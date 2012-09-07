typedef unsigned int logflagtype;

enum {
	LFT_CURLVERB	= 1<<0,
	LFT_PARSE	= 1<<1,
	LFT_PARSEERR	= 1<<2,
	LFT_SOCKTRACE	= 1<<3,
	LFT_SOCKERR	= 1<<4,
	LFT_TPANEL	= 1<<5,
	LFT_TWITACT	= 1<<6,
	LFT_DBTRACE	= 1<<7,
	LFT_DBERR	= 1<<8,
	LFT_ZLIBTRACE	= 1<<9,
	LFT_ZLIBERR	= 1<<10,
	LFT_OTHERTRACE	= 1<<11,
	LFT_OTHERERR	= 1<<12,
	LFT_USERREQ	= 1<<13,
	LFT_PENDTRACE	= 1<<14,
};

enum logflagdefs {
	lfd_stringmask=LFT_CURLVERB|LFT_PARSE|LFT_PARSEERR|LFT_SOCKTRACE|LFT_SOCKERR|LFT_TPANEL|LFT_TWITACT|LFT_DBTRACE|LFT_DBERR|LFT_ZLIBTRACE|LFT_ZLIBERR|LFT_OTHERTRACE|LFT_OTHERERR|LFT_USERREQ|LFT_PENDTRACE,
	lfd_allmask=LFT_CURLVERB|LFT_PARSE|LFT_PARSEERR|LFT_SOCKTRACE|LFT_SOCKERR|LFT_TPANEL|LFT_TWITACT|LFT_DBTRACE|LFT_DBERR|LFT_ZLIBTRACE|LFT_ZLIBERR|LFT_OTHERTRACE|LFT_OTHERERR|LFT_USERREQ|LFT_PENDTRACE,
	lfd_err=LFT_SOCKERR|LFT_DBERR|LFT_ZLIBERR|LFT_PARSEERR|LFT_OTHERERR,
	lfd_defaultwin=lfd_err|LFT_USERREQ,
};

extern logflagtype currentlogflags;

struct log_object {
	logflagtype lo_flags;
	virtual void log_str(logflagtype logflags, const wxString &str)=0;
	log_object(logflagtype flagmask);
	virtual ~log_object();
};

struct log_window : public log_object, public wxFrame {
	std::queue<wxString> pending;
	bool isshown;
	wxTextCtrl *txtct;

	void log_str(logflagtype logflags, const wxString &str);
	log_window(wxWindow *parent, logflagtype flagmask, bool show=true);
	~log_window();
	void LWShow(bool shown=true);
	void OnFrameClose(wxCloseEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnClear(wxCommandEvent &event);
	void OnClose(wxCommandEvent &event);
	void OnDumpPending(wxCommandEvent &event);
	void OnDumpTPanelWins(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

struct log_file : public log_object {
	FILE *fp;
	bool closefpondel;
	log_file(logflagtype flagmask, const char *filename);
	log_file(logflagtype flagmask, FILE *fp_, bool closefpondel_=false);
	~log_file();
	void log_str(logflagtype logflags, const wxString &str);
};

void LogMsgRaw(logflagtype logflags, const wxString &str);
void LogMsgProcess(logflagtype logflags, const wxString &str);
void Update_currentlogflags();

#define LogMsg(l, s) if( currentlogflags & (l) ) LogMsgProcess(l, s)
#define LogMsgFormat(l, ...) if( currentlogflags & (l) ) LogMsgProcess(l, wxString::Format(__VA_ARGS__))

extern log_window *globallogwindow;
logflagtype StrToLogFlags(const wxString &str);

void dump_pending_acc(logflagtype logflags, const wxString &indent, const wxString &indentstep, taccount *acc);
void dump_pending_tpaneldbloadmap(logflagtype logflags, const wxString &indent);
void dump_tpanel_scrollwin_data(logflagtype logflags, const wxString &indent, const wxString &indentstep, tpanelparentwin_nt *tppw);