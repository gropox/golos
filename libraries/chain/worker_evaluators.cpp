#include <golos/protocol/worker_operations.hpp>
#include <golos/protocol/exceptions.hpp>
#include <golos/chain/steem_evaluator.hpp>
#include <golos/chain/database.hpp>
#include <golos/chain/steem_objects.hpp>
#include <golos/chain/worker_objects.hpp>

namespace golos { namespace chain {

    void worker_proposal_evaluator::do_apply(const worker_proposal_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_proposal_operation");

        const auto& comment = _db.get_comment(o.author, o.permlink);

        GOLOS_CHECK_LOGIC(comment.parent_author == STEEMIT_ROOT_POST_PARENT,
            logic_exception::worker_proposal_can_be_created_only_on_post,
            "Worker proposal can be created only on post");

        const auto now = _db.head_block_time();

        const auto* wpo = _db.find_worker_proposal(o.author, o.permlink);

        if (wpo) {
            _db.modify(*wpo, [&](worker_proposal_object& wpo) {
                wpo.type = o.type;
                wpo.modified = now;
            });
            return;
        }

        _db.create<worker_proposal_object>([&](worker_proposal_object& wpo) {
            wpo.author = o.author;
            wpo.permlink = comment.permlink;
            wpo.type = o.type;
            wpo.state = worker_proposal_state::created;
            wpo.created = now;
        });
    }

    void worker_proposal_delete_evaluator::do_apply(const worker_proposal_delete_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_proposal_delete_operation");

        const auto& wpo = _db.get_worker_proposal(o.author, o.permlink);

        GOLOS_CHECK_LOGIC(wpo.state == worker_proposal_state::created,
            logic_exception::cannot_delete_worker_proposal_with_approved_techspec,
            "Cannot delete worker proposal with approved techspec");

        const auto& wto_idx = _db.get_index<worker_techspec_index, by_worker_proposal>();
        auto wto_itr = wto_idx.find(std::make_tuple(o.author, o.permlink));
        GOLOS_CHECK_LOGIC(wto_itr == wto_idx.end(),
            logic_exception::cannot_delete_worker_proposal_with_techspecs,
            "Cannot delete worker proposal with techspecs");

        _db.remove(wpo);
    }

    void worker_proposal_fund_evaluator::do_apply(const worker_proposal_fund_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_proposal_fund_operation");

        const auto& wpo = _db.get_worker_proposal(o.author, o.permlink);

        GOLOS_CHECK_LOGIC(wpo.state == worker_proposal_state::created,
            logic_exception::cannot_fund_worker_proposal_with_approved_techspec,
            "Cannot fund worker proposal with approved techspec");

        // TODO: allow to add funds
        GOLOS_CHECK_LOGIC(wpo.deposit.amount == 0,
            logic_exception::proposal_is_already_funded,
            "Proposal is already funded");

        const auto& funder = _db.get_account(o.funder);
        GOLOS_CHECK_BALANCE(funder, MAIN_BALANCE, o.amount);
        _db.adjust_balance(funder, -o.amount);

        _db.modify(wpo, [&](worker_proposal_object& wpo) {
            wpo.deposit = o.amount;
        });
    }

    void worker_techspec_evaluator::do_apply(const worker_techspec_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_techspec_operation");

        const auto now = _db.head_block_time();

        const auto& comment = _db.get_comment(o.author, o.permlink);

        GOLOS_CHECK_LOGIC(comment.parent_author == STEEMIT_ROOT_POST_PARENT,
            logic_exception::worker_techspec_can_be_created_only_on_post,
            "Worker techspec can be created only on post");

        const auto* wpo = _db.find_worker_proposal(o.worker_proposal_author, o.worker_proposal_permlink);

        GOLOS_CHECK_LOGIC(wpo,
            logic_exception::worker_techspec_can_be_created_only_for_existing_proposal,
            "Worker techspec can be created only for existing proposal");

        GOLOS_CHECK_LOGIC(wpo->state == worker_proposal_state::created,
            logic_exception::this_worker_proposal_already_has_approved_techspec,
            "This worker proposal already has approved techspec");

        const auto& wto_idx = _db.get_index<worker_techspec_index, by_worker_proposal>();
        auto wto_itr = wto_idx.find(std::make_tuple(o.worker_proposal_author, o.worker_proposal_permlink));
        if (wto_itr != wto_idx.end()) {
            GOLOS_CHECK_LOGIC(o.specification_cost.symbol == wto_itr->specification_cost.symbol,
                logic_exception::cannot_change_cost_symbol,
                "Cannot change cost symbol");
            GOLOS_CHECK_LOGIC(o.development_cost.symbol == wto_itr->development_cost.symbol,
                logic_exception::cannot_change_cost_symbol,
                "Cannot change cost symbol");

            _db.modify(*wto_itr, [&](worker_techspec_object& wto) {
                wto.modified = now;
                wto.specification_cost = o.specification_cost;
                wto.specification_eta = o.specification_eta;
                wto.development_cost = o.development_cost;
                wto.development_eta = o.development_eta;
                wto.payments_count = o.payments_count;
                wto.payments_interval = o.payments_interval;
            });

            return;
        }

        _db.create<worker_techspec_object>([&](worker_techspec_object& wto) {
            wto.author = o.author;
            wto.permlink = comment.permlink;
            wto.worker_proposal_author = o.worker_proposal_author;
            from_string(wto.worker_proposal_permlink, o.worker_proposal_permlink);
            wto.created = now;
            wto.specification_cost = o.specification_cost;
            wto.specification_eta = o.specification_eta;
            wto.development_cost = o.development_cost;
            wto.development_eta = o.development_eta;
            wto.payments_count = o.payments_count;
            wto.payments_interval = o.payments_interval;
        });
    }

    void worker_techspec_delete_evaluator::do_apply(const worker_techspec_delete_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_techspec_delete_operation");

        const auto& wto = _db.get_worker_techspec(o.author, o.permlink);

        const auto& wpo = _db.get_worker_proposal(wto.worker_proposal_author, wto.worker_proposal_permlink);

        GOLOS_CHECK_LOGIC(wpo.state < worker_proposal_state::payment,
            logic_exception::cannot_delete_worker_techspec_for_paying_proposal,
            "Cannot delete worker techspec for paying proposal");

        if (wpo.approved_techspec_author == wto.author && wpo.approved_techspec_permlink == wto.permlink) {
            _db.modify(wpo, [&](worker_proposal_object& wpo) {
                wpo.state = worker_proposal_state::created;
            });
        }

        _db.remove(wto);
    }

    void worker_techspec_approve_evaluator::do_apply(const worker_techspec_approve_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_techspec_approve_operation");

        auto approver_witness = _db.get_witness(o.approver);
        GOLOS_CHECK_LOGIC(approver_witness.schedule == witness_object::top19,
            logic_exception::approver_of_techspec_should_be_in_top19_of_witnesses,
            "Approver of techspec should be in Top 19 of witnesses");

        const auto& wto = _db.get_worker_techspec(o.author, o.permlink);

        const auto& wpo = _db.get_worker_proposal(wto.worker_proposal_author, wto.worker_proposal_permlink);

        GOLOS_CHECK_LOGIC(wpo.approved_techspec_permlink.empty(),
            logic_exception::techspec_is_already_approved,
            "Techspec is already approved");

        const auto& wtao_idx = _db.get_index<worker_techspec_approve_index, by_techspec_approver>();
        auto wtao_itr = wtao_idx.find(std::make_tuple(o.author, o.permlink, o.approver));

        if (o.state == worker_techspec_approve_state::abstain) {
            if (wtao_itr != wtao_idx.end()) {
                _db.remove(*wtao_itr);
            }

            return;
        }

        if (wtao_itr != wtao_idx.end()) {
            _db.modify(*wtao_itr, [&](worker_techspec_approve_object& wtao) {
                wtao.state = o.state;
            });
        } else {
            _db.create<worker_techspec_approve_object>([&](worker_techspec_approve_object& wtao) {
                wtao.approver = o.approver;
                wtao.author = o.author;
                from_string(wtao.permlink, o.permlink);
                wtao.state = o.state;
            });
        }

        if (o.state == worker_techspec_approve_state::approve) {
            auto approvers = 0;

            wtao_itr = wtao_idx.lower_bound(std::make_tuple(o.author, o.permlink));
            for (; wtao_itr != wtao_idx.end()
                    && wtao_itr->author == o.author && to_string(wtao_itr->permlink) == o.permlink; ++wtao_itr) {
                auto witness = _db.find_witness(wtao_itr->approver);
                if (witness && witness->schedule == witness_object::top19 && wtao_itr->state == worker_techspec_approve_state::approve) {
                    approvers++;
                }
            }

            if (approvers < STEEMIT_MAJOR_VOTED_WITNESSES) {
                return;
            }

            _db.modify(wpo, [&](worker_proposal_object& wpo) {
                wpo.approved_techspec_author = o.author;
                from_string(wpo.approved_techspec_permlink, o.permlink);
                wpo.state = worker_proposal_state::techspec;
            });

            auto budget = wto.development_cost + wto.specification_cost;
            auto append = budget - wpo.deposit;
            if (append.amount > 0) {
                const auto& gpo = _db.get_dynamic_global_properties();

                GOLOS_CHECK_LOGIC(gpo.total_worker_fund_steem.amount >= append.amount,
                   logic_exception::insufficient_funds_in_worker_fund,
                   "Insufficient funds in worker fund");

                _db.modify(gpo, [&](dynamic_global_property_object& gpo) {
                    gpo.total_worker_fund_steem -= append;
                });

                _db.modify(wpo, [&](worker_proposal_object& wpo) {
                    wpo.deposit += append;
                });
            }
        }
    }

    void worker_result_fill_evaluator::do_apply(const worker_result_fill_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_result_fill_operation");

        const auto now = _db.head_block_time();

        GOLOS_CHECK_LOGIC(o.completion_date <= now,
            logic_exception::work_completion_date_cannot_be_in_future,
            "Work completion date cannot be in future");

        const auto& comment = _db.get_comment(o.author, o.permlink);

        GOLOS_CHECK_LOGIC(comment.parent_author == STEEMIT_ROOT_POST_PARENT,
            logic_exception::worker_result_can_be_created_only_on_post,
            "Worker result can be created only on post");

        const auto& wto = _db.get_worker_techspec(o.author, o.worker_techspec_permlink);

        const auto* wto_result = _db.find_worker_result(o.author, o.permlink);
        GOLOS_CHECK_LOGIC(!wto_result,
            logic_exception::this_post_already_used_as_worker_result,
            "This post already used as worker result");

        const auto& wpo = _db.get_worker_proposal(wto.worker_proposal_author, wto.worker_proposal_permlink);

        GOLOS_CHECK_LOGIC(wpo.approved_techspec_author == o.author && wpo.approved_techspec_permlink == wto.permlink
                && wpo.state == worker_proposal_state::work,
            logic_exception::worker_result_can_be_created_only_for_techspec_in_work,
            "Worker result can be created only for techspec in work");

        _db.modify(wto, [&](worker_techspec_object& wto) {
            from_string(wto.worker_result_permlink, o.permlink);

            if (o.completion_date != time_point_sec::min()) {
                wto.completion_date = o.completion_date;
            } else {
                wto.completion_date = now;
            }
        });

        _db.modify(wpo, [&](worker_proposal_object& wpo) {
            wpo.state = worker_proposal_state::witnesses_review;
        });
    }

    void worker_result_clear_evaluator::do_apply(const worker_result_clear_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_result_clear_operation");

        const auto& wto = _db.get_worker_result(o.author, o.permlink);

        const auto& wpo = _db.get_worker_proposal(wto.worker_proposal_author, wto.worker_proposal_permlink);

        GOLOS_CHECK_LOGIC(wpo.state < worker_proposal_state::payment,
            logic_exception::cannot_delete_worker_result_for_paying_proposal,
            "Cannot delete worker result for paying proposal");

        _db.modify(wpo, [&](worker_proposal_object& wpo) {
            wpo.state = worker_proposal_state::work;
        });

        _db.modify(wto, [&](worker_techspec_object& wto) {
            wto.worker_result_permlink.clear();
            wto.completion_date = time_point::min();
        });
    }

    void worker_result_approve_evaluator::do_apply(const worker_result_approve_operation& o) {
        ASSERT_REQ_HF(STEEMIT_HARDFORK_0_20__1013, "worker_result_approve_operation");

        auto approver_witness = _db.get_witness(o.approver);
        GOLOS_CHECK_LOGIC(approver_witness.schedule == witness_object::top19,
            logic_exception::approver_of_result_should_be_in_top19_of_witnesses,
            "Approver of result should be in Top 19 of witnesses");

        const auto& wto = _db.get_worker_result(o.author, o.permlink);

        const auto& wpo = _db.get_worker_proposal(wto.worker_proposal_author, wto.worker_proposal_permlink);

        if (o.state == worker_techspec_approve_state::disapprove) {
            GOLOS_CHECK_LOGIC(wpo.state == worker_proposal_state::work || wpo.state == worker_proposal_state::witnesses_review,
                logic_exception::worker_proposal_should_be_in_work_or_review_state_to_disapprove,
                "Worker proposal should be in work or review state to disapprove");
        } else if (o.state == worker_techspec_approve_state::approve) {
            GOLOS_CHECK_LOGIC(wpo.state == worker_proposal_state::witnesses_review,
                logic_exception::worker_proposal_should_be_in_review_state_to_approve,
                "Worker proposal should be in review state to approve");
        }

        const auto& wrao_idx = _db.get_index<worker_result_approve_index, by_result_approver>();
        auto wrao_itr = wrao_idx.find(std::make_tuple(o.author, o.permlink, o.approver));

        if (o.state == worker_techspec_approve_state::abstain) {
            if (wrao_itr != wrao_idx.end()) {
                _db.remove(*wrao_itr);
            }

            return;
        }

        if (wrao_itr != wrao_idx.end()) {
            _db.modify(*wrao_itr, [&](worker_result_approve_object& wrao) {
                wrao.state = o.state;
            });
        } else {
            _db.create<worker_result_approve_object>([&](worker_result_approve_object& wrao) {
                wrao.approver = o.approver;
                wrao.author = o.author;
                from_string(wrao.permlink, o.permlink);
                wrao.state = o.state;
            });
        }

        auto count_approvers = [&](auto state) {
            auto approvers = 0;
            wrao_itr = wrao_idx.lower_bound(std::make_tuple(o.author, o.permlink));
            for (; wrao_itr != wrao_idx.end()
                    && wrao_itr->author == o.author && to_string(wrao_itr->permlink) == o.permlink; ++wrao_itr) {
                auto witness = _db.find_witness(wrao_itr->approver);
                if (witness && witness->schedule == witness_object::top19 && wrao_itr->state == state) {
                    approvers++;
                }
            }
            return approvers;
        };

        if (o.state == worker_techspec_approve_state::disapprove) {
            auto disapprovers = count_approvers(worker_techspec_approve_state::disapprove);

            if (disapprovers >= STEEMIT_SUPER_MAJOR_VOTED_WITNESSES) {
                _db.modify(wpo, [&](worker_proposal_object& wpo) {
                    wpo.state = worker_proposal_state::closed;
                });
            }
        } else if (o.state == worker_techspec_approve_state::approve) {
            auto approvers = count_approvers(worker_techspec_approve_state::approve);

            if (approvers >= STEEMIT_MAJOR_VOTED_WITNESSES) {
                _db.modify(wpo, [&](worker_proposal_object& wpo) {
                    wpo.state = worker_proposal_state::payment;

                    wpo.deposit -= wto.specification_cost;

                    wpo.next_cashout_time = _db.head_block_time() + wto.payments_interval;
                    wpo.payment_beginning_time = wpo.next_cashout_time;
                });

                _db.adjust_balance(_db.get_account(wto.author), wto.specification_cost);

                _db.push_virtual_operation(techspec_reward_operation(wto.author, to_string(wto.permlink), wto.specification_cost));
            }
        }
    }

} } // golos::chain
