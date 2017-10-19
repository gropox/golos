#ifndef GOLOS_VERSION_STATE_HPP
#define GOLOS_VERSION_STATE_HPP

#include <steemit/version/version.hpp>

namespace steemit {
    namespace version {
        class state {
        public:
            static state &instance() {
                static state static_instance;
                return static_instance;
            }

            protocol::hardfork_version current_version;

        private:
            state() {
            }

            state(const state &root);

            state &operator=(const state &);
        };
    }
}

#endif //GOLOS_VERSION_STATE_HPP
