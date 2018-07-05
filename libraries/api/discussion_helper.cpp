#include <golos/api/discussion_helper.hpp>
#include <golos/api/comment_api_object.hpp>
#include <golos/chain/account_object.hpp>
#include <golos/chain/steem_objects.hpp>
#include <fc/io/json.hpp>
#include <boost/algorithm/string.hpp>


namespace golos { namespace api {

    comment_metadata get_metadata(const comment_api_object &c) {

        comment_metadata meta;

        if (!c.json_metadata.empty()) {
            try {
                meta = fc::json::from_string(c.json_metadata).as<comment_metadata>();
            } catch (const fc::exception& e) {
                // Do nothing on malformed json_metadata
            }
        }

        std::set<std::string> lower_tags;

        std::size_t tag_limit = 5;
        for (const auto& name : meta.tags) {
            if (lower_tags.size() > tag_limit) {
                break;
            }
            auto value = boost::trim_copy(name);
            if (value.empty()) {
                continue;
            }
            boost::to_lower(value);
            lower_tags.insert(value);
        }

        meta.tags.swap(lower_tags);

        boost::trim(meta.language);
        boost::to_lower(meta.language);

        return meta;
    }


    boost::multiprecision::uint256_t to256(const fc::uint128_t& t) {
        boost::multiprecision::uint256_t result(t.high_bits());
        result <<= 65;
        result += t.low_bits();
        return result;
    }

    struct discussion_helper::impl final {
    public:
        impl() = delete;
        impl(
            golos::chain::database& db,
            std::function<void(const golos::chain::database&, const account_name_type&, fc::optional<share_type>&)> fill_reputation,
            std::function<void(const golos::chain::database&, discussion&)> fill_promoted)
            : database_(db),
              fill_reputation_(fill_reputation),
              fill_promoted_(fill_promoted) {
        }
        ~impl() = default;

        discussion create_discussion(const std::string& author) const ;

        discussion create_discussion(const comment_object& o) const ;

        void select_active_votes(
            std::vector<vote_state>& result, uint32_t& total_count,
            const std::string& author, const std::string& permlink, uint32_t limit
        ) const ;

        void set_pending_payout(discussion& d) const;

        void set_url(discussion& d) const;

        golos::chain::database& database() {
            return database_;
        }

        golos::chain::database& database() const {
            return database_;
        }

        comment_api_object create_comment_api_object(const comment_object & o) const;

        discussion get_discussion(const comment_object& c, uint32_t vote_limit) const;

        get_comment_content_res get_comment_content(const golos::chain::database & db, const comment_object & o);

    private:
        golos::chain::database& database_;
        std::function<void(const golos::chain::database&, const account_name_type&, fc::optional<share_type>&)> fill_reputation_;
        std::function<void(const golos::chain::database&, discussion&)> fill_promoted_;
        std::function<get_comment_content_res(const golos::chain::database&, const comment_object &)> get_comment_content_callback;
    };

// create_comment_api_object 
    comment_api_object discussion_helper::create_comment_api_object(const comment_object & o) const {
        return pimpl->create_comment_api_object(o);
    }

    comment_api_object discussion_helper::impl::create_comment_api_object(const comment_object & o) const {
        comment_api_object result;
        auto & db = database_;

        result.id = o.id;
        result.parent_author = o.parent_author;
        result.parent_permlink = to_string(o.parent_permlink);
        result.author = o.author;
        result.permlink = to_string(o.permlink);
        result.last_update = o.last_update;
        result.created = o.created;
        result.active = o.active;
        result.last_payout = o.last_payout;
        result.depth = o.depth;
        result.children = o.children;
        result.children_rshares2 = o.children_rshares2;
        result.net_rshares = o.net_rshares;
        result.abs_rshares = o.abs_rshares;
        result.vote_rshares = o.vote_rshares;
        result.children_abs_rshares = o.children_abs_rshares;
        result.cashout_time = o.cashout_time;
        result.max_cashout_time = o.max_cashout_time;
        result.total_vote_weight = o.total_vote_weight;
        result.reward_weight = o.reward_weight;
        result.total_payout_value = o.total_payout_value;
        result.curator_payout_value = o.curator_payout_value;
        result.author_rewards = o.author_rewards;
        result.net_votes = o.net_votes;
        result.mode = o.mode;
        result.root_comment = o.root_comment;
        result.max_accepted_payout = o.max_accepted_payout;
        result.percent_steem_dollars = o.percent_steem_dollars;
        result.allow_replies = o.allow_replies;
        result.allow_votes = o.allow_votes;
        result.allow_curation_rewards = o.allow_curation_rewards;

        for (auto& route : o.beneficiaries) {
            result.beneficiaries.push_back(route);
        }

        if ( get_comment_content_callback ) {
            auto content = get_comment_content_callback(database(), o);

            result.title = content.title;
            result.body = content.body;
            result.json_metadata = content.json_metadata;
        }

        if (o.parent_author == STEEMIT_ROOT_POST_PARENT) {
            result.category = to_string(o.parent_permlink);
        } else {
            result.category = to_string(db.get<comment_object, by_id>(o.root_comment).parent_permlink);
        }

        return result;
    }


// get_comment_content
    get_comment_content_res discussion_helper::impl::get_comment_content(const golos::chain::database & db, const comment_object & o) {
        return get_comment_content_callback(db, o);        
    }
// get_discussion
    discussion discussion_helper::impl::get_discussion(const comment_object& c, uint32_t vote_limit) const {
        discussion d = create_discussion(c);
        set_url(d);
        set_pending_payout(d);
        select_active_votes(d.active_votes, d.active_votes_count, d.author, d.permlink, vote_limit);
        return d;
    }

    discussion discussion_helper::get_discussion(const comment_object& c, uint32_t vote_limit) const {
        return pimpl->get_discussion(c, vote_limit);
    }
//

// select_active_votes
    void discussion_helper::impl::select_active_votes(
        std::vector<vote_state>& result, uint32_t& total_count,
        const std::string& author, const std::string& permlink, uint32_t limit
    ) const {
        const auto& comment = database().get_comment(author, permlink);
        const auto& idx = database().get_index<comment_vote_index>().indices().get<by_comment_voter>();
        comment_object::id_type cid(comment.id);
        total_count = 0;
        result.clear();
        for (auto itr = idx.lower_bound(cid); itr != idx.end() && itr->comment == cid; ++itr, ++total_count) {
            if (result.size() < limit) {
                const auto& vo = database().get(itr->voter);
                vote_state vstate;
                vstate.voter = vo.name;
                vstate.weight = itr->weight;
                vstate.rshares = itr->rshares;
                vstate.percent = itr->vote_percent;
                vstate.time = itr->last_update;
                fill_reputation_(database(), vo.name, vstate.reputation);
                result.emplace_back(vstate);
            }
        }
    }

    void discussion_helper::select_active_votes(
        std::vector<vote_state>& result, uint32_t& total_count,
        const std::string& author, const std::string& permlink, uint32_t limit
    ) const {
        pimpl->select_active_votes(result, total_count, author, permlink, limit);
    }
//
// set_pending_payout
    void discussion_helper::impl::set_pending_payout(discussion& d) const {
        auto& db = database();

        fill_promoted_(db, d);

        const auto& props = db.get_dynamic_global_properties();
        const auto& hist = db.get_feed_history();
        asset pot = props.total_reward_fund_steem;
        if (!hist.current_median_history.is_null()) {
            pot = pot * hist.current_median_history;
        }

        u256 total_r2 = to256(props.total_reward_shares2);

        if (props.total_reward_shares2 > 0) {
            auto vshares = db.calculate_vshares(d.net_rshares.value > 0 ? d.net_rshares.value : 0);

            u256 r2 = to256(vshares); //to256(abs_net_rshares);
            r2 *= pot.amount.value;
            r2 /= total_r2;

            u256 tpp = to256(d.children_rshares2);
            tpp *= pot.amount.value;
            tpp /= total_r2;

            d.pending_payout_value = asset(static_cast<uint64_t>(r2), pot.symbol);
            d.total_pending_payout_value = asset(static_cast<uint64_t>(tpp), pot.symbol);
        }

        fill_reputation_(db, d.author, d.author_reputation);

        if (d.parent_author != STEEMIT_ROOT_POST_PARENT) {
            d.cashout_time = db.calculate_discussion_payout_time(db.get<comment_object>(d.id));
        }

        if (d.body.size() > 1024 * 128) {
            d.body = "body pruned due to size";
        }
        if (d.parent_author.size() > 0 && d.body.size() > 1024 * 16) {
            d.body = "comment pruned due to size";
        }

        set_url(d);
    }

    void discussion_helper::set_pending_payout(discussion& d) const {
        pimpl->set_pending_payout(d);
    }
//
// set_url
    void discussion_helper::impl::set_url(discussion& d) const {
        comment_object cm = database().get<comment_object, by_id>(d.root_comment);

        comment_api_object root = create_comment_api_object( cm );

        d.root_title = root.title;
        d.url = "/" + root.category + "/@" + root.author + "/" + root.permlink;

        if (root.id != d.id) {
            d.url += "#@" + d.author + "/" + d.permlink;
        }
    }

    void discussion_helper::set_url(discussion& d) const {
        pimpl->set_url(d);
    }
//
// create_discussion
    discussion discussion_helper::impl::create_discussion(const std::string& author) const {
        auto dis = discussion();
        fill_reputation_(database_, author, dis.author_reputation);
        return dis;
    }

    discussion discussion_helper::impl::create_discussion(const comment_object& o) const {
        auto x = create_comment_api_object(o);
        return discussion(x);
    }

    discussion discussion_helper::create_discussion(const std::string& author) const {
        return pimpl->create_discussion(author);
    }

    discussion discussion_helper::create_discussion(const comment_object& o) const {
        return pimpl->create_discussion(o);
    }

    discussion_helper::discussion_helper(
        golos::chain::database& db,
        std::function<void(const golos::chain::database&, const account_name_type&, fc::optional<share_type>&)> fill_reputation,
        std::function<void(const golos::chain::database&, discussion&)> fill_promoted
    ) {
        pimpl = std::make_unique<impl>(db, fill_reputation, fill_promoted);
    }

    discussion_helper::~discussion_helper() = default;

//
} } // golos::api
