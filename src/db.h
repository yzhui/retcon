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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_DB
#define HGUARD_SRC_DB

#include "univdefs.h"
#include "tweetidset.h"
#include "flags.h"
#include "hash.h"
#include "media_id_type.h"
#include "ptr_types.h"
#include "set.h"
#include "observer_ptr.h"
#include <string>
#include <memory>
#include <deque>
#include <queue>
#include <set>
#include <forward_list>
#include <wx/event.h>

struct media_entity;
struct tweet;
struct userdatacontainer;
struct dbconn;
struct dbiothread;
enum class MEF : unsigned int;

enum class DBSM {
	QUIT = 1,
	INSERTTWEET,
	UPDATETWEET,
	SELTWEET,
	INSERTUSER,
	MSGLIST,
	INSERTACC,
	INSERTMEDIA,
	UPDATEMEDIAMSG,
	DELACC,
	UPDATETWEETSETFLAGS,
	FUNCTION,
};

struct dbreplyevtstruct {
	std::deque<std::pair<wxEvtHandler *, std::unique_ptr<wxEvent> > > reply_list;
};

struct dbsendmsg {
	DBSM type;

	dbsendmsg(DBSM type_) : type(type_) { }
	virtual ~dbsendmsg() { }
};

struct dbsendmsg_list : public dbsendmsg {
	dbsendmsg_list() : dbsendmsg(DBSM::MSGLIST) { }

	std::vector<std::unique_ptr<dbsendmsg> > msglist;
};

struct dbsendmsg_callback : public dbsendmsg {
	dbsendmsg_callback(DBSM type_) : dbsendmsg(type_) { }
	dbsendmsg_callback(DBSM type_, wxEvtHandler *targ_, WXTYPE cmdevtype_, int winid_ = wxID_ANY ) :
		dbsendmsg(type_), targ(targ_), cmdevtype(cmdevtype_), winid(winid_) { }

	wxEvtHandler *targ;
	WXTYPE cmdevtype;
	int winid;

	void SendReply(std::unique_ptr<dbsendmsg> data, dbiothread *th);
};

struct dbinserttweetmsg : public dbsendmsg {
	dbinserttweetmsg() : dbsendmsg(DBSM::INSERTTWEET) { }

	std::string statjson;
	std::string dynjson;
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;
};

struct dbupdatetweetmsg : public dbsendmsg {
	dbupdatetweetmsg() : dbsendmsg(DBSM::UPDATETWEET) { }

	std::string dynjson;
	uint64_t id;
	uint64_t flags;
};

struct dbrettweetdata {
	char *statjson = nullptr;   //free when done
	char *dynjson = nullptr;    //free when done
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;

	dbrettweetdata() { }
	~dbrettweetdata() {
		if(statjson) free(statjson);
		if(dynjson) free(dynjson);
	}
	dbrettweetdata(const dbrettweetdata& that) = delete;
};

enum class DBSTMF {
	NO_ERR          = 1<<1,
	NET_FALLBACK    = 1<<2,
	CLEARNOUPDF     = 1<<3,
};
template<> struct enum_traits<DBSTMF> { static constexpr bool flags = true; };

struct dbseltweetmsg : public dbsendmsg_callback {
	dbseltweetmsg() : dbsendmsg_callback(DBSM::SELTWEET) { }

	flagwrapper<DBSTMF> flags = 0;
	container::set<uint64_t> id_set;         // ids to select
	std::deque<dbrettweetdata> data;         // return data
};

struct dbseltweetmsg_netfallback : public dbseltweetmsg {
	dbseltweetmsg_netfallback() : dbseltweetmsg() {
		flags |= DBSTMF::NET_FALLBACK;
	}

	unsigned int dbindex = 0;    //for the use of the main thread only
};

struct dbinsertusermsg : public dbsendmsg {
	dbinsertusermsg() : dbsendmsg(DBSM::INSERTUSER) { }
	uint64_t id;
	std::string json;
	std::string cached_profile_img_url;
	time_t createtime;
	uint64_t lastupdate;
	shb_iptr cached_profile_img_hash;
	unsigned char *mentionindex;    //already packed and compressed, must be malloced
	size_t mentionindex_size;
	uint64_t profile_img_last_used;
	unsigned char *dmindex;    //already packed and compressed, must be malloced
	size_t dmindex_size;
};

struct dbinsertaccmsg : public dbsendmsg_callback {
	dbinsertaccmsg() : dbsendmsg_callback(DBSM::INSERTACC) { }

	std::string name;            //account name
	std::string dispname;        //account name
	uint64_t userid;
	unsigned int dbindex;        //return data
};

struct dbdelaccmsg : public dbsendmsg {
	dbdelaccmsg() : dbsendmsg(DBSM::DELACC) { }

	unsigned int dbindex = 0;
};

struct dbinsertmediamsg : public dbsendmsg {
	dbinsertmediamsg() : dbsendmsg(DBSM::INSERTMEDIA) { }
	media_id_type media_id;
	std::string url;
	uint64_t lastused;
};

enum class DBUMMT {
	THUMBCHECKSUM = 1,
	FULLCHECKSUM,
	FLAGS,
	LASTUSED,
};

struct dbupdatemediamsg : public dbsendmsg {
	dbupdatemediamsg(DBUMMT type_) : dbsendmsg(DBSM::UPDATEMEDIAMSG), update_type(type_) { }
	media_id_type media_id;
	shb_iptr chksm;
	flagwrapper<MEF> flags;
	uint64_t lastused;
	DBUMMT update_type;
};

struct dbupdatetweetsetflagsmsg : public dbsendmsg {
	dbupdatetweetsetflagsmsg(tweetidset &&ids_, uint64_t setmask_, uint64_t unsetmask_) : dbsendmsg(DBSM::UPDATETWEETSETFLAGS), ids(ids_), setmask(setmask_), unsetmask(unsetmask_) { }

	tweetidset ids;
	uint64_t setmask;
	uint64_t unsetmask;
};

enum class HDBSF {
	NOPENDINGS         = 1<<0,
};
template<> struct enum_traits<HDBSF> { static constexpr bool flags = true; };

bool DBC_Init(const std::string &filename);
void DBC_DeInit();
void DBC_AsyncWriteBackState();
void DBC_SendMessage(std::unique_ptr<dbsendmsg> msg);
void DBC_SendMessageOrAddToList(std::unique_ptr<dbsendmsg> msg, optional_observer_ptr<dbsendmsg_list> msglist);
void DBC_SendMessageBatched(std::unique_ptr<dbsendmsg> msg);
observer_ptr<dbsendmsg_list> DBC_GetMessageBatchQueue();
void DBC_SendAccDBUpdate(std::unique_ptr<dbinsertaccmsg> insmsg);
void DBC_InsertMedia(media_entity &me, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_UpdateMedia(media_entity &me, DBUMMT update_type, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_InsertNewTweet(tweet_ptr_p tobj, std::string statjson, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_HandleDBSelTweetMsg(dbseltweetmsg &msg, flagwrapper<HDBSF> flags);
void DBC_SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f);
void DBC_PrepareStdTweetLoadMsg(dbseltweetmsg &loadmsg);

#endif
