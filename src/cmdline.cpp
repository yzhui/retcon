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
#include "cmdline.h"
#include "cfg.h"
#include "log.h"
#include "log-impl.h"
#include "util.h"
#include "SimpleOpt.h"
#include "retcon.h"
#include "version.h"
#include <wx/frame.h>

typedef CSimpleOptTempl<wxChar> CSO;

enum { OPT_LOGWIN, OPT_FILE, OPT_STDERR, OPT_FILEAUTO, OPT_DATADIR, OPT_FFLUSH, OPT_READONLY, OPT_ACCSDSBD, OPT_LOGMEMUSAGE, OPT_VERSION,
		OPT_RESCAN_TWEETS };

CSO::SOption g_rgOptions[] =
{
	{ OPT_LOGWIN,       wxT("-w"),             SO_REQ_SHRT  },
	{ OPT_FILE,         wxT("-f"),             SO_REQ_SHRT  },
	{ OPT_STDERR,       wxT("-s"),             SO_REQ_SHRT  },
	{ OPT_FILEAUTO,     wxT("-a"),             SO_REQ_SHRT  },
	{ OPT_DATADIR,      wxT("-d"),             SO_REQ_SHRT  },
	{ OPT_LOGWIN,       wxT("--log-window"),   SO_REQ_SHRT  },
	{ OPT_FILE,         wxT("--log-file"),     SO_REQ_SHRT  },
	{ OPT_FILEAUTO,     wxT("--log-file-auto"),SO_REQ_SHRT  },
	{ OPT_STDERR,       wxT("--log-stderr"),   SO_REQ_SHRT  },
	{ OPT_DATADIR,      wxT("--data-dir"),     SO_REQ_SHRT  },
	{ OPT_FFLUSH,       wxT("-F"),             SO_NONE      },
	{ OPT_FFLUSH,       wxT("--log-fflush"),   SO_NONE      },
	{ OPT_READONLY,     wxT("-r"),             SO_NONE      },
	{ OPT_READONLY,     wxT("--read-only"),    SO_NONE      },
	{ OPT_ACCSDSBD,     wxT("-b"),             SO_NONE      },
	{ OPT_ACCSDSBD,     wxT("--accs-disabled"),SO_NONE      },
	{ OPT_VERSION,      wxT("-V"),             SO_NONE      },
#ifndef __WINDOWS__
	{ OPT_LOGMEMUSAGE,  wxT("--log-mem-usage"),SO_NONE      },
#endif
	{ OPT_RESCAN_TWEETS,wxT("--rescan-tweets-table"),SO_NONE},
	{ OPT_VERSION,      wxT("--version"),      SO_NONE      },

	SO_END_OF_OPTIONS
};

static const wxChar* cmdlineargerrorstr(ESOError err) {
	switch (err) {
	case SO_OPT_INVALID:
		return wxT("Unrecognized option");
	case SO_OPT_MULTIPLE:
		return wxT("Option matched multiple strings");
	case SO_ARG_INVALID:
		return wxT("Option does not accept argument");
	case SO_ARG_INVALID_TYPE:
		return wxT("Invalid argument format");
	case SO_ARG_MISSING:
		return wxT("Required argument is missing");
	case SO_ARG_INVALID_DATA:
		return wxT("Option argument appears to be another option");
	default:
		return wxT("Unknown Error");
	}
}

int cmdlineproc(wxChar **argv, int argc) {
	CSO args(argc, argv, g_rgOptions, SO_O_CLUMP | SO_O_EXACT | SO_O_SHORTARG | SO_O_FILEARG | SO_O_CLUMP_ARGD | SO_O_NOSLASH);
	while (args.Next()) {
		if (args.LastError() != SO_SUCCESS) {
			wxLogError(wxT("Command line processing error: %s, arg: %s"), cmdlineargerrorstr(args.LastError()), args.OptionText());
			return 1;
		}
		switch(args.OptionId()) {
			case OPT_LOGWIN: {
				LOGT flagmask = StrToLogFlags(stdstrwx(args.OptionArg()));
				if (!globallogwindow) {
					new log_window(nullptr, flagmask, true);
				} else {
					globallogwindow->lo_flags = flagmask;
					Update_currentlogflags();
				}
				break;
			}
			case OPT_FILE: {
				if (args.m_nNextOption + 1 > args.m_nLastArg) {
					wxLogError(wxT("Command line processing error: -f/--log-file requires filename argument, arg: %s"), args.OptionText());
					return 1;
				}
				LOGT flagmask = StrToLogFlags(stdstrwx(args.OptionArg()));
				wxString filename = args.m_argv[args.m_nNextOption++];
				new log_file(flagmask, filename.char_str());
				break;
			}
			case OPT_FILEAUTO: {
				time_t now = time(nullptr);
				wxString filename = wxT("retcon-log-")+rc_wx_strftime(wxT("%Y%m%dT%H%M%SZ.log"), gmtime(&now), now, false);
				LOGT flagmask = StrToLogFlags(stdstrwx(args.OptionArg()));
				new log_file(flagmask, filename.char_str());
				break;
			}
			case OPT_STDERR: {
				LOGT flagmask = StrToLogFlags(stdstrwx(args.OptionArg()));
				new log_file(flagmask, stderr);
				break;
			}
			case OPT_DATADIR: {
				wxGetApp().datadir = stdstrwx(args.OptionArg());
				break;
			}
			case OPT_FFLUSH: {
				logimpl_flags |= LOGIMPLF::FFLUSH;
				break;
			}
			case OPT_LOGMEMUSAGE: {
				logimpl_flags |= LOGIMPLF::LOGMEMUSAGE;
				break;
			}
			case OPT_READONLY: {
				gc.readonlymode = true;
				break;
			}
			case OPT_ACCSDSBD: {
				gc.allaccsdisabled = true;
				break;
			}
			case OPT_RESCAN_TWEETS: {
				gc.rescan_tweets_table = true;
				break;
			}
			case OPT_VERSION: {
				wxSafeShowMessage(wxT("Version"), wxString::Format(wxT("%s (%s)"), appversionname.c_str(), appbuildversion.c_str()));
				wxGetApp().terms_requested++;
				break;
			}
		}
	}
	return 0;
}
