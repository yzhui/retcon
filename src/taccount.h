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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TACCOUNT
#define HGUARD_SRC_TACCOUNT

#include "univdefs.h"
#include "cfg.h"
#include "user_relationship.h"
#include "twit-common.h"
#include "twitcurlext-common.h"
#include "rbfs.h"
#include "flags.h"
#include "twit.h"
#include "observer_ptr.h"
#include "map.h"
#include <wx/event.h>
#include <wx/string.h>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <forward_list>
#include <list>

struct twitcurlext;
struct userdatacontainer;
struct restbackfillstate;
struct tweet;
class raii_set;

class wxTimer;
class wxTimerEvent;
class oAuth;

enum class PENDING_BITS : unsigned char;

typedef enum { ACT_NOTDONE, ACT_INPROGRESS, ACT_FAILED, ACT_DONE } ACT_STATUS;

enum {
	TAF_WINID_RESTTIMER = 1,
	TAF_FAILED_PENDING_CONN_RETRY_TIMER,
	TAF_STREAM_RESTART_TIMER,
	TAF_NOACC_PENDING_CONTENT_TIMER,
};

struct taccount_cfg {
	unsigned int dbindex = 0;
	genoptconf cfg;
	wxString conk;
	wxString cons;
	bool userenabled = false;
	uint64_t max_tweet_id = 0;
	uint64_t max_mention_id = 0;
	uint64_t max_recvdm_id = 0;
	uint64_t max_sentdm_id = 0;
	wxString dispname;
	bool ur_ifollow_have_list = false;
	bool ur_followsme_have_list = false;
	uint64_t last_block_fetch_time = 0;
	uint64_t last_mute_fetch_time = 0;
	uint64_t last_no_rt_fetch_time = 0;
	unsigned int sort_order = 0;

	void CFGWriteOut(DBWriteConfig &twfc) const;
	void CFGReadInBase(DBReadConfig &twfc);
	void UnShareStrings();
};

struct taccount : public wxEvtHandler, public taccount_cfg, std::enable_shared_from_this<taccount> {
	wxString name;

	bool ssl;
	bool userstreams;
	SRM stream_reply_mode;
	bool stream_currently_reply_all = false;
	bool stream_drop_blocked;
	bool stream_drop_muted;
	bool stream_drop_no_rt;

	enum class TAF {
		STREAM_UP             = 1<<0,
	};
	flagwrapper<TAF> ta_flags = 0;
	unsigned long restinterval;    //seconds

	unsigned long expire_tweets_days; // 0 means no expiry

	uint64_t &GetMaxId(RBFS_TYPE type) {
		switch (type) {
			case RBFS_TWEETS: return max_tweet_id;
			case RBFS_MENTIONS: return (gc.assumementionistweet) ? max_tweet_id : max_mention_id;
			case RBFS_RECVDM: return max_recvdm_id;
			case RBFS_SENTDM: return max_sentdm_id;
			default: return max_tweet_id;
		}
	}

	time_t last_stream_start_time = 0;
	time_t last_stream_end_time = 0;
	udc_ptr usercont;
	container::map<uint64_t, user_relationship> user_relations; // this must be sorted

	//any tweet or DM in this list *must* be either in ad.tweetobjs, or in the database
	tweetidset tweet_ids;
	tweetidset dm_ids;

	useridset blocked_users;
	useridset muted_users;
	useridset no_rt_users;

	std::unordered_map<uint64_t, udc_ptr> pendingusers;
	std::forward_list<restbackfillstate> pending_rbfs_list;

	std::deque<std::unique_ptr<twitcurlext>> failed_pending_conns;	//strict subset of cp.activeset
	std::unique_ptr<wxTimer> pending_failed_conn_retry_timer;
	std::unique_ptr<wxTimer> stream_restart_timer;
	void CheckFailedPendingConns();
	void AddFailedPendingConn(std::unique_ptr<twitcurlext> conn);
	void OnFailedPendingConnRetryTimer(wxTimerEvent& event);
	void OnStreamRestartTimer(wxTimerEvent& event);
	bool CanRestartStreamingConn() const;
	void TryRestartStreamingConnNow();

	bool enabled = false;
	bool init = false;
	bool active = false;
	bool streaming_on = false;
	unsigned int stream_fail_count = 0;
	bool rest_on = false;
	ACT_STATUS verifycredstatus = ACT_NOTDONE;
	bool beinginsertedintodb = false;
	time_t last_rest_backfill = 0;
	std::unique_ptr<wxTimer> rest_timer;
	std::unique_ptr<wxTimer> noacc_pending_content_timer;

	std::function<void(observer_ptr<twitcurlext>)> TwitCurlExtHook;

	void ClearAllUserRelationshipsByType(user_relationship::UR_TYPE type, std::vector<uint64_t> *currentset = nullptr, std::vector<uint64_t> *pendingset = nullptr);
	void GetSetUserRelationshipsByType(user_relationship::UR_TYPE type, std::vector<uint64_t> *currentset = nullptr, std::vector<uint64_t> *pendingset = nullptr);
	void SetUserRelationship(uint64_t userid, flagwrapper<user_relationship::URF> flags, time_t optime);
	bool IsFollowingUser(uint64_t userid);

	void StartRestGetTweetBackfill(uint64_t start_tweet_id /*lower limit, exclusive*/, uint64_t end_tweet_id /*upper limit, inclusive*/,
			unsigned int max_tweets_to_read, RBFS_TYPE type = RBFS_TWEETS, uint64_t userid = 0);
	void ExecRBFS(observer_ptr<restbackfillstate> rbfs);
	void StartRestQueryPendings();
	void DoPostAction(twitcurlext &lasttce);
	void DoPostAction(flagwrapper<PAF> postflags);
	void GetRestBackfill();
	void CheckUpdateBlockLists();
	void LookupFriendships(uint64_t userid);
	void GetUsersFollowingMeList();
	void HandleUsersFollowingMeList(std::vector<uint64_t> userids, bool complete);
	void HandleUserIFollowList(std::vector<uint64_t> userids, bool complete);
	void HandleUserRelationshipListCommon(std::vector<uint64_t> userids, bool complete, user_relationship::UR_TYPE type, bool &listvalid, user_relationship::URF setto);
	void NotifyDiffUserRelationshipList(user_relationship::UR_TYPE type, const std::vector<uint64_t> &oldset, const std::vector<uint64_t> &oldpending);
	void NotifyUserRelationshipChange(uint64_t userid, user_relationship::URF flags);
	void NotifyTweetFavouriteEvent(uint64_t tweetid, uint64_t userid, bool unfavourite);
	void NotifyBlockListChange(BLOCKTYPE type, uint64_t userid, bool now_blocked);
	useridset &GetBlockList(BLOCKTYPE type);
	void UpdateBlockListFetchTime(BLOCKTYPE type);
	void ReplaceBlockList(BLOCKTYPE type, useridset new_ids);
	void SetUserIdBlockedState(uint64_t user_id, BLOCKTYPE type, bool blocked);

	void MarkUserPending(udc_ptr_p user);
	bool MarkPendingOrHandle(tweet_ptr_p t, flagwrapper<ARRIVAL> arr);

	void OnRestTimer(wxTimerEvent& event);
	void SetupRestBackfillTimer();
	void DeleteRestBackfillTimer();

	void CFGReadIn(DBReadConfig &twfc);
	void CFGParamConv();
	bool TwDoOAuth(wxWindow *pf, twitcurlext &twit);
	void PostAccVerifyInit();
	void Exec();
	void CalcEnabled();
	std::unique_ptr<twitcurlext> PrepareNewStreamConn();
	wxString GetStatusString(bool notextifok = false);
	taccount(genoptconf *incfg = nullptr);

	void ApplyNewTwitCurlExtHook(observer_ptr<twitcurlext> tce);
	void SetNewTwitCurlExtHook(std::function<void(observer_ptr<twitcurlext>)> func);
	void ClearNewTwitCurlExtHook();

	void OnNoAccPendingContentTimer(wxTimerEvent& event);
	void NoAccPendingContentEvent();
	void NoAccPendingContentCheck();

	std::string DumpStateString() const;
	void LogStateChange(const std::string &tag, raii_set *finaliser = nullptr);

	//After structure filled in, but before it's actually used for anything
	//Can check values in here
	void Setup();

	//Set dispname from usercont
	void SetName();

	void setOAuthParameters(oAuth &auth) const;

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<taccount::TAF> { static constexpr bool flags = true; };

struct user_relationship_change_guard {
	taccount &acc;
	uint64_t userid;
	flagwrapper<user_relationship::URF> rel;

	user_relationship_change_guard(taccount &acc_, uint64_t userid_);
	~user_relationship_change_guard();
};

void AccountChangeTrigger();
bool GetAccByDBIndex(unsigned int dbindex, std::shared_ptr<taccount> &acc);
void SortAccounts();

extern std::vector<std::shared_ptr<taccount>> alist;

#endif
