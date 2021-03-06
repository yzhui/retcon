//  retcon
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "retcon.h"
#include "version.h"
#include "raii.h"
#include "log-impl.h"
#include "alldata.h"
#include "cfg.h"
#include "signal.h"
#include "cmdline.h"
#include "socket-ops.h"
#include "mainui.h"
#include "taccount.h"
#include "db.h"
#include "filter/filter-ops.h"
#include "util.h"
#include "tpanel-data.h"
#include "threadutil.h"
#include "twit.h"
#ifdef __WINDOWS__
#include "tpanel.h"
#endif
#include "debug.h"
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <cstdio>
#include <cstdlib>

IMPLEMENT_APP(retcon)

DEFINE_EVENT_TYPE(wxextRetcon_Evt)

BEGIN_EVENT_TABLE(retcon, wxApp)
	EVT_MENU(ID_Quit,  retcon::OnQuitMsg)
	EVT_COMMAND(ID_ExecPendings, wxextRetcon_Evt, retcon::OnExecPendingsMsg)
	EVT_COMMAND(ID_ThreadPoolExec, wxextRetcon_Evt, retcon::OnExecThreadPoolMsg)
END_EVENT_TABLE()

bool retcon::OnInit() {
	raii_set rs;
	//wxApp::OnInit();	//don't call this, it just calls the default command line processor
	SetAppName(appname);
	InitStdoutFilter();
	InitWxLogger();
	rs.add([&]() { DeInitWxLogger(); });

#if defined(__WXGTK__)
	InitGlibLogger();
	rs.add([&]() { DeInitGlibLogger(); });
#endif

	::wxInitAllImageHandlers();
	srand((unsigned int) time(nullptr));
	datadir = stdstrwx(wxStandardPaths::Get().GetUserDataDir());
	tmpdir = make_temp_dir("retcon-");
	cmdlineproc(argv, argc);
	if (terms_requested) {
		return false;
	}
	if (!globallogwindow) {
		new log_window(nullptr, LOGT::GROUP_LOGWINDEF, false);
	}
	if (!datadir.empty() && datadir.back() == '/') {
		datadir.pop_back();
	}
	wxString wxdatadir = wxstrstd(datadir);
	if (!::wxDirExists(wxdatadir)) {
		::wxMkdir(wxdatadir, 0700);
	}
	InitCFGDefaults();
	SetTermSigHandler();
	sm.InitMultiIOHandler();
	rs.add([&]() {
		sm.DeInitMultiIOHandler();
	});
	bool res = DBC_Init(datadir + "/retcondb.sqlite3");
	if (!res) {
		return false;
	}
	rs.add([&]() {
		DBC_DeInit();
	});
	if (terms_requested) {
		return false;
	}
	pool.reset(new ThreadPool::Pool(gc.threadpoollimit));

	InitGlobalFilters();

	RestoreWindowLayout();
	if (mainframelist.empty()) {
		mainframe *mf = new mainframe( appversionname, wxPoint(50, 50), wxSize(450, 340));

		if (alist.empty() && ad.tpanels.empty()) {
			//everything is empty, maybe new user
			//make 3 basic auto tpanels to make things more obvious
			auto flags = TPF::AUTO_ALLACCS | TPF::DELETEONWINCLOSE;
			auto tpt = tpanel::MkTPanel("", "", flags | TPF::AUTO_TW, 0);
			tpt->MkTPanelWin(mf, true);
			auto tpm = tpanel::MkTPanel("", "", flags | TPF::AUTO_MN, 0);
			tpm->MkTPanelWin(mf, false);
			auto tpd = tpanel::MkTPanel("", "", flags | TPF::AUTO_DM, 0);
			tpd->MkTPanelWin(mf, false);
		}
	}

	if (terms_requested) return false;

	mainframelist[0]->Show(true);
	for (auto &it : alist) {
		it->Setup();
		it->CalcEnabled();
		it->Exec();
	}

	if (terms_requested) return false;

	rs.cancel();
	return true;
}

int retcon::OnExit() {
	LogMsg(LOGT::OTHERTRACE, "retcon::OnExit");
	sm.DeInitMultiIOHandler();
	pool.reset();
	DBC_DeInit();
	for (const observer_ptr<temp_file_holder> &it : temp_file_set) {
		LogMsgFormat(LOGT::FILEIOTRACE, "retcon::OnExit, resetting: %s", cstr(it->GetFilename()));
		it->Reset();
	}
	if (!::wxRmdir(wxstrstd(tmpdir))) {
		LogMsgFormat(LOGT::FILEIOERR, "retcon::OnExit, could not remove temp dir: %s", cstr(tmpdir));
	}
	DebugFinalChecks();
	DeInitWxLogger();
	std::exit(0);
}

int retcon::FilterEvent(wxEvent& event) {
	static unsigned int antirecursion = 0;
	if (antirecursion) {
		return -1;
	}

	antirecursion++;
	#ifdef __WINDOWS__
	if (event.GetEventType() == wxEVT_MOUSEWHEEL) {
		if (GetMainframeAncestor((wxWindow *) event.GetEventObject())) {
			if (RedirectMouseWheelEvent((wxMouseEvent &) event)) {
				antirecursion--;
				return 1;
			}
		}
	}
	#endif
	antirecursion--;

	return -1;
}

void retcon::OnQuitMsg(wxCommandEvent &event) {
	LogMsgFormat(LOGT::OTHERTRACE, "retcon::OnQuitMsg, about to call wxExit(), %d termination requests, %d mainframes, top win: %p, popup recursion: %d",
			terms_requested, mainframelist.size(), GetTopWindow(), popuprecursion);
	wxExit();
}

void retcon::OnExecPendingsMsg(wxCommandEvent &event) {
	auto current_pendings = std::move(pendings);
	pendings.clear();

	for (auto &it : current_pendings) {
		it();
	}
}

void retcon::EnqueuePending(std::function<void()> &&f) {
	pendings.emplace_back(std::move(f));
	wxCommandEvent evt(wxextRetcon_Evt, ID_ExecPendings);
	AddPendingEvent(evt);
}

void retcon::OnExecThreadPoolMsg(wxCommandEvent &event) {
	long jobnum = event.GetExtraLong();
	auto it = pool_post_jobs.find(jobnum);
	if (it != pool_post_jobs.end()) {
		it->second();
		pool_post_jobs.erase(it);
	}
}

void retcon::EnqueueThreadJob(std::function<void()> &&worker_thread_job, std::function<void()> &&main_thread_post_job) {
	if (!pool->GetThreadLimit()) {
		worker_thread_job();
		main_thread_post_job();
		return;
	}

	struct job_data {
		long jobnum;
		std::function<void()> job;
		wxEvtHandler *target;
	};
	auto data = std::make_shared<job_data>();
	data->jobnum = next_pool_job;
	data->job = std::move(worker_thread_job);
	data->target = this;
	pool_post_jobs[next_pool_job] = std::move(main_thread_post_job);
	next_pool_job++;

	pool->enqueue([data](ThreadPool::Worker &w) {
		data->job();

		wxCommandEvent evt(wxextRetcon_Evt, ID_ThreadPoolExec);
		evt.SetExtraLong(data->jobnum);
		data->target->AddPendingEvent(evt);
	});
}

void retcon::EnqueueThreadJob(std::function<void()> &&worker_thread_job) {
	if (!pool->GetThreadLimit()) {
		worker_thread_job();
		return;
	}

	auto data = std::make_shared<std::function<void()> >(std::move(worker_thread_job));
	pool->enqueue([data](ThreadPool::Worker &w) {
		(*data)();
	});
}
