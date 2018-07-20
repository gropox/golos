#pragma once

#include <golos/protocol/base.hpp>
#include <golos/protocol/types.hpp>
#include <golos/chain/steem_object_types.hpp>
#include <chainbase/chainbase.hpp>
#include <golos/protocol/operation_util.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace golos { namespace plugins { namespace private_message {

    using namespace golos::protocol;
    using namespace chainbase;
    using namespace golos::chain;

#ifndef PRIVATE_MESSAGE_SPACE_ID
#define PRIVATE_MESSAGE_SPACE_ID 6
#endif

    enum private_message_object_type {
        message_object_type = (PRIVATE_MESSAGE_SPACE_ID << 8)
    };

    class message_object:
        public object<message_object_type, message_object> {
    public:
        template<typename Constructor, typename Allocator>
        message_object(Constructor&& c, allocator <Allocator> a)
            : encrypted_message(a) {
            c(*this);
        }

        id_type id;

        account_name_type from;
        account_name_type to;
        public_key_type from_memo_key;
        public_key_type to_memo_key;
        uint64_t sent_time = 0; /// used as seed to secret generation
        time_point_sec receive_time; /// time received by blockchain
        uint32_t checksum = 0;
        buffer_type encrypted_message;
    };

    using message_id_type = message_object::id_type;

    struct message_api_obj {
        message_api_obj(const message_object& o)
            : id(o.id),
              from(o.from),
              to(o.to),
              from_memo_key(o.from_memo_key),
              to_memo_key(o.to_memo_key),
              sent_time(o.sent_time),
              receive_time(o.receive_time),
              checksum(o.checksum),
              encrypted_message(o.encrypted_message.begin(), o.encrypted_message.end()) {
        }

        message_api_obj() = default;

        message_id_type id;
        account_name_type from;
        account_name_type to;
        public_key_type from_memo_key;
        public_key_type to_memo_key;
        uint64_t sent_time;
        time_point_sec receive_time;
        uint32_t checksum;
        std::vector<char> encrypted_message;
    };

    struct by_to_date;
    struct by_from_date;

    using namespace boost::multi_index;

    using message_index = multi_index_container<
        message_object,
        indexed_by<
            ordered_unique<
                tag<by_id>,
                member<message_object, message_id_type, &message_object::id>>,
            ordered_unique<
                tag<by_to_date>,
                composite_key<
                    message_object,
                    member<message_object, account_name_type, &message_object::to>,
                    member<message_object, time_point_sec, &message_object::receive_time>,
                    member<message_object, message_id_type, &message_object::id>>,
                composite_key_compare<
                    std::less<string>,
                    std::greater<time_point_sec>,
                    std::less<message_id_type>>>,
            ordered_unique<
                tag<by_from_date>,
                composite_key<
                    message_object,
                    member<message_object, account_name_type, &message_object::from>,
                    member<message_object, time_point_sec, &message_object::receive_time>,
                    member<message_object, message_id_type, &message_object::id>>,
                composite_key_compare<
                    std::less<string>,
                    std::greater<time_point_sec>,
                    std::less<message_id_type>>>>,
        allocator<message_object>>;

    struct private_message_operation: public base_operation {
        account_name_type from;
        account_name_type to;
        public_key_type from_memo_key;
        public_key_type to_memo_key;
        uint64_t sent_time = 0; /// used as seed to secret generation
        uint32_t checksum = 0;
        std::vector<char> encrypted_message;

        void validate() const;

        void get_required_posting_authorities(flat_set<account_name_type>& a) const {
            a.insert(from);
        }
    };

    using private_message_plugin_operation = fc::static_variant<private_message_operation>;

} } } // golos::plugins::private_message

FC_REFLECT(
    (golos::plugins::private_message::private_message_operation),
    (from)(to)(from_memo_key)(to_memo_key)(sent_time)(checksum)(encrypted_message))

FC_REFLECT_TYPENAME((golos::plugins::private_message::private_message_plugin_operation))
DECLARE_OPERATION_TYPE(golos::plugins::private_message::private_message_plugin_operation)