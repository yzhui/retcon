#include "retcon.h"
#ifdef __WINDOWS__
#include <windows.h>
#endif
#include <zlib.h>

const unsigned char jsondictionary[]="<a href=\"http://retweet_countsourcetextentitiesindiceshashtagsurlsdisplayexpandedjpgpnguser_mentionsmediaidhttptweetusercreatedfavoritedscreen_namein_reply_to_user_idprofileprotectedfollowdescriptionfriends"
				"typesizesthe[{\",\":\"}]";
const unsigned char profimgdictionary[]="http://https://si0.twimg.com/profile_images/imagesmallnormal.png.jpg.jpeg.gif";

const char *startup_sql=
"CREATE TABLE IF NOT EXISTS tweets(id INTEGER PRIMARY KEY NOT NULL, statjson BLOB, dynjson BLOB, userid INTEGER, userrecipid INTEGER, flags INTEGER, timestamp INTEGER);"
"CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY NOT NULL, json BLOB, cachedprofimgurl BLOB, createtimestamp INTEGER, lastupdatetimestamp INTEGER);"
"CREATE TABLE IF NOT EXISTS acc(id INTEGER PRIMARY KEY NOT NULL, name TEXT, json BLOB, tweetids BLOB, dmids BLOB);";

const char *sql[DBPSC_NUM_STATEMENTS]={
	"INSERT INTO tweets(id, statjson, dynjson, userid, userrecipid, flags, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?);",
	"UPDATE tweets SET dynjson = ?, flags = ? WHERE id == ?;",
	"BEGIN;",
	"COMMIT;",
	"INSERT OR REPLACE INTO users(id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp) VALUES (?, ?, ?, ?, ?);",
	"INSERT INTO acc (name) VALUES (?);",
	"UPDATE acc SET tweetids = ?, dmids = ? WHERE id == ?;",
	"SELECT statjson, dynjson, userid, userrecipid, flags, timestamp FROM tweets WHERE id == ?",
};

int busy_handler_callback(void *ptr, int count) {
	if(count<20) {
		unsigned int sleeplen=25<<count;
		wxThread *th=wxThread::This();
		if(th) th->Sleep(sleeplen);
		else wxMilliSleep(sleeplen);
		return 0;
	}
	else return 1;
}

sqlite3_stmt *dbpscache::GetStmt(sqlite3 *adb, DBPSC_TYPE type) {
	if(!stmts[type]) {
		sqlite3_prepare_v2(adb, sql[type], -1, &stmts[type], 0);
	}
	return stmts[type];
}

int dbpscache::ExecStmt(sqlite3 *adb, DBPSC_TYPE type) {
	sqlite3_stmt *stmt=GetStmt(adb, type);
	sqlite3_step(stmt);
	return sqlite3_reset(stmt);
}


void dbpscache::DeAllocAll() {
	for(unsigned int i=DBPSC_START; i<DBPSC_NUM_STATEMENTS; i++) {
		if(stmts[i]) {
			sqlite3_finalize(stmts[i]);
			stmts[i]=0;
		}
	}
}

static bool TagToDict(unsigned char tag, const unsigned char *&dict, size_t &dict_size) {
	switch(tag) {
		case 'Z': {
			dict=0;
			dict_size=0;
			return true;
		}
		case 'J': {
			dict=jsondictionary;
			dict_size=sizeof(jsondictionary);
			return true;
		}
		case 'P': {
			dict=profimgdictionary;
			dict_size=sizeof(profimgdictionary);
			return true;
		}
		default: return false;
	}
}

#define HEADERSIZE 5

static unsigned char *DoCompress(const void *in, size_t insize, size_t &sz, unsigned char tag='Z', bool *iscompressed=0) {
	unsigned char *data;
	const unsigned char *dict;
	size_t dict_size;
	bool compress=TagToDict(tag, dict, dict_size);
	if(compress && insize>=100) {
		z_stream strm;
		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		deflateInit(&strm, 9);

		if(dict) deflateSetDictionary(&strm, dict, dict_size);
		size_t maxsize=deflateBound(&strm, insize);
		data=(unsigned char *) malloc(maxsize+HEADERSIZE);
		data[0]=tag;
		data[1]=(insize>>24)&0xFF;
		data[2]=(insize>>16)&0xFF;
		data[3]=(insize>>8)&0xFF;
		data[4]=(insize>>0)&0xFF;
		strm.avail_in=insize;
		strm.next_in=(unsigned char *) in;
		strm.avail_out=maxsize;
		strm.next_out=data+HEADERSIZE;
		int res=deflate(&strm, Z_FINISH);
		//OutputDebugStringW(wxString::Format(wxT("deflate: %d, %d, %d"), res, strm.avail_in, strm.avail_out).wc_str());
		sz=HEADERSIZE+maxsize-strm.avail_out;
		deflateEnd(&strm);
		if(iscompressed) *iscompressed=true;
	}
	else {
		data=(unsigned char *) malloc(insize+1);
		data[0]='T';
		memcpy(data+1, in, insize);
		sz=insize+1;
		if(iscompressed) *iscompressed=false;
	}

	static size_t cumin=0;
	static size_t cumout=0;
	cumin+=insize;
	cumout+=sz;
	//OutputDebugStringW(wxString::Format(wxT("zlib compress: %d -> %d, cum: %f"), insize, sz, (double) cumout/ (double) cumin).wc_str());

	return data;
}

static unsigned char *DoCompress(const std::string &in, size_t &sz, unsigned char tag='Z', bool *iscompressed=0) {
	return DoCompress(in.data(), in.size(), sz, tag, iscompressed);
}

enum {
	BINDCF_NONTEXT		= 1<<0,
};

static void bind_compressed(sqlite3_stmt* stmt, int num, const std::string &in, unsigned char tag='Z', unsigned int flags=0) {
	size_t comsize;
	bool iscompressed;
	unsigned char *com=DoCompress(in, comsize, tag, &iscompressed);
	if(iscompressed || flags&BINDCF_NONTEXT) sqlite3_bind_blob(stmt, num, com, comsize, &free);
	else sqlite3_bind_text(stmt, num, (const char *) com, comsize, &free);
}

static char *DoDecompress(const unsigned char *in, size_t insize, size_t &outsize) {
	if(!insize) {
		//OutputDebugStringA("DoDecompress: insize=0");
		outsize=0;
		return 0;
	}
	const unsigned char *dict;
	size_t dict_size;
	switch(in[0]) {
		case 'T': {
			outsize=insize-1;
			char *data=(char *) malloc(outsize+1);
			memcpy(data, in+1, outsize);
			data[outsize]=0;
			return data;
		}
		default: {
			bool compress=TagToDict(in[0], dict, dict_size);
			if(compress) break;
			else {
				//OutputDebugStringA("DoDecompress: Bad tag");
				outsize=0;
				return 0;
			}
		}
	}
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.next_in = (unsigned char*) in+HEADERSIZE;
	strm.avail_in = insize-HEADERSIZE;
	inflateInit(&strm);
	outsize=0;
	for(unsigned int i=1; i<5; i++) {
		outsize<<=8;
		outsize+=in[i];
	}
	//OutputDebugStringW(wxString::Format(wxT("DoDecompress: insize %d, outsize %d"), insize, outsize).wc_str());
	unsigned char *data=(unsigned char *) malloc(outsize+1);
	strm.next_out=data;
	strm.avail_out=outsize;
	while(true) {
		int res=inflate(&strm, Z_FINISH);
		//OutputDebugStringW(wxString::Format(wxT("inflate: %d, %d, %d"), res, strm.avail_in, strm.avail_out).wc_str());
		//if(strm.msg) OutputDebugStringA(strm.msg);
		if(res==Z_NEED_DICT) {
			if(dict) inflateSetDictionary(&strm, dict, dict_size);
			else {
				outsize=0;
				inflateEnd(&strm);
				free(data);
				//OutputDebugStringA("DoDecompress: Wants dictionary");
				return 0;
			}
		}
		else if(res==Z_OK) continue;
		else if(res==Z_STREAM_END) break;
		else {
			outsize=0;
			inflateEnd(&strm);
			free(data);
			//OutputDebugStringA("DoDecompress: res!=Z_STREAM_END");
			return 0;
		}
	}

	inflateEnd(&strm);
	data[outsize]=0;

	//OutputDebugStringW(wxString::Format(wxT("zlib decompress: %d -> %d, text: %s"), insize, outsize, wxstrstd((const char*) data).c_str()).wc_str());
	return (char *) data;
}

//the result should be freed when done if non-zero
static char *column_get_compressed(sqlite3_stmt* stmt, int num, size_t &outsize) {
	const unsigned char *data= (const unsigned char *) sqlite3_column_blob(stmt, num);
	int size=sqlite3_column_bytes(stmt, num);
	return DoDecompress(data, size, outsize);
}

//the result should be freed when done if non-zero
static char *column_get_compressed_and_parse(sqlite3_stmt* stmt, int num, rapidjson::Document &dc) {
	size_t str_size;
	char *str=column_get_compressed(stmt, num, str_size);
	if(str) {
		if(dc.ParseInsitu<0>(str).HasParseError()) {
			dc.SetNull();
		}
	}
	else dc.SetNull();
	return str;
}

static void ProcessMessage(sqlite3 *db, dbsendmsg *msg, bool &ok, dbpscache &cache) {
	switch(msg->type) {
		case DBSM_QUIT:
			ok=false;
			break;
		case DBSM_INSERTTWEET: {
			dbinserttweetmsg *m=(dbinserttweetmsg*) msg;
			sqlite3_stmt *stmt=cache.GetStmt(db, DBPSC_INSTWEET);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->id);
			bind_compressed(stmt, 2, m->statjson, 'J');
			bind_compressed(stmt, 3, m->dynjson, 'J');
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->user1);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->user2);
			sqlite3_bind_int64(stmt, 6, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 7, (sqlite3_int64) m->timestamp);
			sqlite3_step(stmt);
			sqlite3_reset(stmt);
			break;
		}
		case DBSM_UPDATETWEET: {
			dbupdatetweetmsg *m=(dbupdatetweetmsg*) msg;
			sqlite3_stmt *stmt=cache.GetStmt(db, DBPSC_UPDTWEET);
			bind_compressed(stmt, 1, m->dynjson, 'J');
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->id);
			sqlite3_step(stmt);
			sqlite3_reset(stmt);
			break;
		}
		case DBSM_SELTWEET: {
			dbseltweetmsg *m=(dbseltweetmsg*) msg;
			sqlite3_stmt *stmt=cache.GetStmt(db, DBPSC_SELTWEET);
			std::forward_list<dbrettweetdata> recv_data;
			for(auto it=m->id_set.cbegin(); it!=m->id_set.cend(); ++it) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) (*it));
				int res=sqlite3_step(stmt);
				//wxLogWarning(wxT("dbconn::OnTpanelTweetLoadFromDB got tweet: id:%" wxLongLongFmtSpec "d, statjson: %s, dynjson: %s"), dt.id, wxstrstd(dt.statjson).c_str(), wxstrstd(dt.dynjson).c_str());
				if(res==SQLITE_ROW) {
					recv_data.emplace_front();
					dbrettweetdata &rd=recv_data.front();
					size_t outsize;
					rd.id=(*it);
					rd.statjson=column_get_compressed(stmt, 0, outsize);
					rd.dynjson=column_get_compressed(stmt, 1, outsize);
					rd.user1=(uint64_t) sqlite3_column_int64(stmt, 2);
					rd.user2=(uint64_t) sqlite3_column_int64(stmt, 3);
					rd.flags=(uint64_t) sqlite3_column_int64(stmt, 4);
					rd.timestamp=(uint64_t) sqlite3_column_int64(stmt, 5);
				}
				sqlite3_reset(stmt);
			}
			if(!recv_data.empty()) {
				m->data=std::move(recv_data);
				m->SendReply(m);
				return;
			}
			break;
		}
		case DBSM_INSERTUSER: {
			dbinsertusermsg *m=(dbinsertusermsg*) msg;
			sqlite3_stmt *stmt=cache.GetStmt(db, DBPSC_INSUSER);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->id);
			bind_compressed(stmt, 2, m->json, 'J');
			bind_compressed(stmt, 3, m->cached_profile_img_url, 'P');
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->createtime);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->lastupdate);
			sqlite3_step(stmt);
			sqlite3_reset(stmt);
			break;
		}
		case DBSM_MSGLIST: {
			cache.ExecStmt(db, DBPSC_BEGIN);
			dbsendmsg_list *m=(dbsendmsg_list*) msg;
			while(!m->msglist.empty()) {
				ProcessMessage(db, m->msglist.front(), ok, cache);
				m->msglist.pop();
			}
			cache.ExecStmt(db, DBPSC_COMMIT);
		}
		default: break;
	}
	delete msg;
}

wxThread::ExitCode dbiothread::Entry() {

	int res=sqlite3_open(filename.c_str(), &db);
	if(res!=SQLITE_OK) return 0;

	sqlite3_busy_handler(db, &busy_handler_callback, 0);

	MsgLoop();

	sqlite3_close(db);

	return 0;
}

void dbiothread::MsgLoop() {
	bool ok=true;
	while(ok) {
		dbsendmsg *msg;
		#ifdef __WINDOWS__
		DWORD num;
		OVERLAPPED *ovlp;
		bool res=GetQueuedCompletionStatus(iocp, &num, (PULONG_PTR) &msg, &ovlp, INFINITE);
		if(!res) {
			return;
		}
		#else
		size_t bytes_to_read=sizeof(msg);
		size_t bytes_read=0;
		while(bytes_to_read) {
			ssize_t l_bytes_read=read(pipefd, ((char *) &msg)+bytes_read, bytes_to_read);
			if(l_bytes_read>=0) {
				bytes_read+=l_bytes_read;
				bytes_to_read-=l_bytes_read;
			}
			else {
				if(l_bytes_read==EINTR) continue;
				else {
					close(pipefd);
					return;
				}
			}
		}
		#endif
		ProcessMessage(db, msg, ok, cache);
	}
}

DEFINE_EVENT_TYPE(wxextDBCONN_NOTIFY)

BEGIN_EVENT_TABLE(dbconn, wxEvtHandler)
EVT_COMMAND(wxDBCONNEVT_ID_TPANELTWEETLOAD, wxextDBCONN_NOTIFY, dbconn::OnTpanelTweetLoadFromDB)
END_EVENT_TABLE()

void dbconn::OnTpanelTweetLoadFromDB(wxCommandEvent &event) {
	dbseltweetmsg *msg=(dbseltweetmsg *) event.GetClientData();
	for(auto it=msg->data.begin(); it!=msg->data.end(); ++it) {
		dbrettweetdata &dt=*it;
		//wxLogWarning(wxT("dbconn::OnTpanelTweetLoadFromDB got tweet: id:%" wxLongLongFmtSpec "d, statjson: %s, dynjson: %s"), dt.id, wxstrstd(dt.statjson).c_str(), wxstrstd(dt.dynjson).c_str());
		std::shared_ptr<tweet> &t=ad.tweetobjs[dt.id];
		if(!t) {
			t=std::make_shared<tweet>();
			t->id=dt.id;
		}
		rapidjson::Document dc;
		if(dt.statjson && !dc.ParseInsitu<0>(dt.statjson).HasParseError() && dc.IsObject()) {
			//wxLogWarning(wxT("dbconn::OnTpanelTweetLoadFromDB about to parse tweet statics"));
			genjsonparser::ParseTweetStatics(dc, t, 0);
		}
		if(dt.dynjson && !dc.ParseInsitu<0>(dt.dynjson).HasParseError() && dc.IsObject()) {
			//wxLogWarning(wxT("dbconn::OnTpanelTweetLoadFromDB about to parse tweet dyn"));
			genjsonparser::ParseTweetDyn(dc, t);
		}
		t->user=ad.GetUserContainerById(dt.user1);
		if(dt.user2) t->user_recipient=ad.GetUserContainerById(dt.user2);
		t->createtime=(time_t) dt.timestamp;
		new (&t->flags) tweet_flags(dt.flags);


		t->user->ImgIsReady(false);				//load any images from saved files if necessary
		if(dt.user2) t->user_recipient->ImgIsReady(false);

		t->lflags&=~TLF_BEINGLOADEDFROMDB;

		//any tweet in the database will also have the relevant user objects as well, hence no risk of a null user
		//does not matter if the user object is not strictly up to date
		auto itpair=tpaneldbloadmap.equal_range(dt.id);
		for(auto it=itpair.first; it!=itpair.second; ++it) (*it).second->PushTweet(t);
		tpaneldbloadmap.erase(itpair.first, itpair.second);
	}
	delete msg;
}

void dbconn::SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist) {
	if(msglist) msglist->msglist.push(msg);
	else SendMessage(msg);
}

void dbconn::SendMessage(dbsendmsg *msg) {
	if(sqlite3_threadsafe()) {
		#ifdef __WINDOWS__
		PostQueuedCompletionStatus(iocp, 0, (ULONG_PTR) msg, 0);
		#else
		write(pipefd, &msg, sizeof(msg));
		#endif
	}
	else {
		bool ok;
		ProcessMessage(syncdb, msg, ok, cache);
	}
}

void dbconn::Init(const std::string &filename /*UTF-8*/) {
	if(isinited) return;
	isinited=true;

	sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
	sqlite3_initialize();

	int res=sqlite3_open(filename.c_str(), &syncdb);
	if(res!=SQLITE_OK) return;

	sqlite3_busy_handler(syncdb, &busy_handler_callback, 0);

	res=sqlite3_exec(syncdb, startup_sql, 0, 0, 0);
	if(res!=SQLITE_OK) return;

	AccountSync(syncdb);
	SyncReadInAllUsers(syncdb);

	if(sqlite3_threadsafe()) {
		th=new dbiothread();
		th->filename=filename;
		#ifdef __WINDOWS__
		iocp=CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
		th->iocp=iocp;
		#else
		int pipefd[2];
		pipe(pipefd);
		th->pipefd=pipefd[0];
		this->pipefd=pipefd[1];
		#endif
		th->Create();
		th->Run();
	}
}

void dbconn::DeInit() {
	if(!isinited) return;
	isinited=false;

	if(sqlite3_threadsafe()) {
		SendMessage(new dbsendmsg(DBSM_QUIT));

		#ifdef __WINDOWS__
		CloseHandle(iocp);
		#else
		close(pipefd);
		#endif
		th->Wait();
		delete th;
	}

	SyncWriteBackAllUsers(syncdb);
	AccountIdListsSync(syncdb);

	sqlite3_close(syncdb);
}

void dbconn::InsertNewTweet(const std::shared_ptr<tweet> &tobj, std::string statjson, dbsendmsg_list *msglist) {
	dbinserttweetmsg *msg=new dbinserttweetmsg();
	msg->statjson=std::move(statjson);
	msg->statjson.push_back((char) 42);	//modify the string to prevent any possible COW semantics
	msg->statjson.resize(msg->statjson.size()-1);
	msg->dynjson=tobj->mkdynjson();
	msg->id=tobj->id;
	msg->user1=tobj->user->id;
	msg->user2=tobj->user_recipient?tobj->user_recipient->id:0;
	msg->timestamp=tobj->createtime;
	msg->flags=tobj->flags.Save();
	SendMessageOrAddToList(msg, msglist);
}

void dbconn::UpdateTweetDyn(const std::shared_ptr<tweet> &tobj, dbsendmsg_list *msglist) {
	dbupdatetweetmsg *msg=new dbupdatetweetmsg();
	msg->dynjson=tobj->mkdynjson();
	msg->id=tobj->id;
	msg->flags=tobj->flags.Save();
	SendMessageOrAddToList(msg, msglist);
}

void dbconn::InsertUser(const std::shared_ptr<userdatacontainer> &u, dbsendmsg_list *msglist) {
	dbinsertusermsg *msg=new dbinsertusermsg();
	msg->id=u->id;
	msg->json=u->mkjson();
	msg->cached_profile_img_url=std::string(u->cached_profile_img_url.begin(), u->cached_profile_img_url.end());	//prevent any COW semantics
	msg->createtime=u->user.createtime;
	msg->lastupdate=u->lastupdate;
	u->lastupdate_wrotetodb=u->lastupdate;
	SendMessageOrAddToList(msg, msglist);
}

//tweetids, dmids are little endian in database
void dbconn::AccountSync(sqlite3 *adb) {
	const char getacc[]="SELECT id, name, tweetids, dmids FROM acc;";
	auto acclist=alist;	//copy list
	sqlite3_stmt *getstmt;
	sqlite3_prepare_v2(adb, getacc, sizeof(getacc)+1, &getstmt, 0);
	do {
		int res=sqlite3_step(getstmt);
		if(res==SQLITE_ROW) {
			unsigned int id=(unsigned int) sqlite3_column_int(getstmt, 0);
			wxString name=wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 1));
			//wxLogWarning(wxT("dbconn::AccountSync: Found %d, %s"), id, name.c_str());
			taccount *acc=0;
			acclist.remove_if([&](const std::shared_ptr<taccount> &t) {
				if(t->name==name) {
					t->dbindex=id;
					acc=t.get();
					return true;
				}
				else return false;
			});
			if(acc) {
				size_t tweetarraysize;
				unsigned char *tweetarray=(unsigned char*) column_get_compressed(getstmt, 2, tweetarraysize);
				//const unsigned char *tweetarray=(const unsigned char*) sqlite3_column_blob(getstmt, 2);
				//tweetarraysize=sqlite3_column_bytes(getstmt, 2);
				tweetarraysize&=~7;
				for(unsigned int i=0; i<tweetarraysize; i+=8) {		//stored in big endian format
					uint64_t id=0;
					for(unsigned int j=0; j<8; j++) id<<=8, id|=tweetarray[i+j];
					acc->tweet_ids.insert(id);
					//wxLogWarning(wxT("dbconn::AccountSync: Found Tweet %" wxLongLongFmtSpec "d"), id);
				}
				size_t dmarraysize;
				unsigned char *dmarray=(unsigned char*) column_get_compressed(getstmt, 3, dmarraysize);
				//const unsigned char *dmarray=(const unsigned char*) sqlite3_column_blob(getstmt, 3);
				//dmarraysize=sqlite3_column_bytes(getstmt, 3);
				dmarraysize&=~7;
				for(unsigned int i=0; i<dmarraysize; i+=8) {		//stored in big endian format
					uint64_t id=0;
					for(unsigned int j=0; j<8; j++) id<<=8, id|=dmarray[i+j];
					acc->dm_ids.insert(id);
					//wxLogWarning(wxT("dbconn::AccountSync: Found DM %" wxLongLongFmtSpec "d"), id);
				}
				free(tweetarray);
				free(dmarray);
			}
		}
		else break;
	} while(true);
	sqlite3_finalize(getstmt);
	if(!acclist.empty()) {
		for(auto it=acclist.begin(); it!=acclist.end(); ++it) {
			SyncInsertNewAccount(adb, **it);
		}
	}
}

inline void writebeuint64(unsigned char* data, uint64_t id) {
	data[0]=(id>>56)&0xFF;
	data[1]=(id>>48)&0xFF;
	data[2]=(id>>40)&0xFF;
	data[3]=(id>>32)&0xFF;
	data[4]=(id>>24)&0xFF;
	data[5]=(id>>16)&0xFF;
	data[6]=(id>>8)&0xFF;
	data[7]=(id>>0)&0xFF;
}

static unsigned char *settoblob(const tweetidset &set, size_t &size) {
	size=set.size()*8;
	unsigned char *data=(unsigned char *) malloc(size);
	unsigned char *curdata=data;
	for(auto it=set.cbegin(); it!=set.cend(); ++it) {
		writebeuint64(curdata, *it);
		curdata+=8;
	}
	return data;
}

static unsigned char *settocompressedblob(const tweetidset &set, size_t &size) {
	size_t insize;
	unsigned char *data=settoblob(set, insize);
	unsigned char *comdata=DoCompress(data, insize, size, 'Z');
	free(data);
	return comdata;
}

void dbconn::AccountIdListsSync(sqlite3 *adb) {
	sqlite3_stmt *setstmt=cache.GetStmt(adb, DBPSC_UPDATEACCIDLISTS);
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		size_t size;
		unsigned char *data;

		data=settocompressedblob((*it)->tweet_ids, size);
		sqlite3_bind_blob(setstmt, 1, data, size, &free);

		data=settocompressedblob((*it)->dm_ids, size);
		sqlite3_bind_blob(setstmt, 2, data, size, &free);

		sqlite3_bind_int(setstmt, 3, (*it)->dbindex);

		sqlite3_step(setstmt);
		sqlite3_reset(setstmt);
	}
}

void dbconn::SyncInsertNewAccount(sqlite3 *adb, taccount &acc) {
	sqlite3_stmt *setstmt=cache.GetStmt(adb, DBPSC_INSERTNEWACC);
	sqlite3_bind_text(setstmt, 1, acc.name.ToUTF8(), -1, SQLITE_TRANSIENT);
	sqlite3_step(setstmt);
	acc.dbindex=(unsigned int) sqlite3_last_insert_rowid(adb);
	sqlite3_reset(setstmt);
}

void dbconn::SyncWriteBackAllUsers(sqlite3 *adb) {
	cache.ExecStmt(adb, DBPSC_BEGIN);
	//sqlite3_exec(adb, "DELETE FROM users", 0, 0, 0);

	sqlite3_stmt *stmt=cache.GetStmt(adb, DBPSC_INSUSER);
	for(auto it=ad.userconts.begin(); it!=ad.userconts.end(); ++it) {
		userdatacontainer *u=it->second.get();
		if(u->lastupdate==u->lastupdate_wrotetodb) continue;	//this user is already in the database and does not need updating
		if(u->user.screen_name.empty()) continue;		//don't bother saving empty user stubs
		sqlite3_bind_int64(stmt, 1, (sqlite3_int64) it->first);
		bind_compressed(stmt, 2, u->mkjson(), 'J');
		bind_compressed(stmt, 3, u->cached_profile_img_url, 'P');
		sqlite3_bind_int64(stmt, 4, (sqlite3_int64) u->user.createtime);
		sqlite3_bind_int64(stmt, 5, (sqlite3_int64) u->lastupdate);
		sqlite3_step(stmt);
		sqlite3_reset(stmt);
	}
	cache.ExecStmt(adb, DBPSC_COMMIT);
}

void dbconn::SyncReadInAllUsers(sqlite3 *adb) {
	const char sql[]="SELECT id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp FROM users;";
	sqlite3_stmt *stmt;
	sqlite3_prepare_v2(adb, sql, sizeof(sql)+1, &stmt, 0);

	do {
		int res=sqlite3_step(stmt);
		if(res==SQLITE_ROW) {
			uint64_t id=(uint64_t) sqlite3_column_int64(stmt, 0);
			std::shared_ptr<userdatacontainer> &ref=ad.userconts[id]=std::make_shared<userdatacontainer>();
			userdatacontainer &u=*ref.get();
			u.id=id;
			u.udc_flags=0;
			rapidjson::Document dc;
			char *json=column_get_compressed_and_parse(stmt, 1, dc);
			if(dc.IsObject()) genjsonparser::ParseUserContents(dc, u.user);
			size_t profimg_size;
			char *profimg=column_get_compressed(stmt, 2, profimg_size);
			u.cached_profile_img_url.assign(profimg, profimg_size);
			if(u.user.profile_img_url.empty()) u.user.profile_img_url.assign(profimg, profimg_size);
			u.user.createtime=(time_t) sqlite3_column_int64(stmt, 3);
			u.lastupdate=(uint64_t) sqlite3_column_int64(stmt, 4);
			if(json) free(json);
			if(profimg) free(profimg);
		}
		else break;
	} while(true);
	sqlite3_finalize(stmt);
}

void dbsendmsg_callback::SendReply(void *data) {
	wxCommandEvent evt(cmdevtype, winid);
	evt.SetClientData(data);
	targ->AddPendingEvent(evt);
}