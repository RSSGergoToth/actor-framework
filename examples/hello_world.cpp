#include <string>
#include <iostream>

#include "caf/all.hpp"

using std::endl;
using std::string;


namespace {

using namespace caf;

struct source_state {
  int x = 0;
};

behavior source(stateful_actor<source_state>* self,
                actor target, size_t num_msgs) {
  auto sh = self->new_stream(target, [=]() -> result<int> {
    auto x = self->state.x++;
    if (x == num_msgs) {
      self->quit();
      return skip();
    }
    return x;
  });
  return {
    [=] {
      // dummy
    }
  };
}

struct sink_state {
  static const char* name;
  size_t received_messages = 0;
  ~sink_state() {
    printf("received messages: %d\n", (int) received_messages);
  }
};

const char* sink_state::name = "foobar-sink";

behavior sink(stateful_actor<sink_state>* self) {
  return {
    [=](int) {
      if (++self->state.received_messages % 10000 == 0) {
        printf("~~~ received messages: %d\n",
               (int) self->state.received_messages);
      }
    }
  };
}

behavior forward_stage(event_based_actor* self) {
  return {
    [=](int x) {
      return x;
    }
  };
}

class config : public actor_system_config {
public:
  size_t num_sources = 20;
  size_t num_msgs = 1000;

  config() {
    opt_group{custom_options_, "global"}
    .add(num_sources, "num-sources,s", "nr. of sources")
    .add(num_msgs, "num-messages,n", "nr. of messages");
  }
};

void caf_main(actor_system& system, const config& cfg) {
  scoped_actor self{system};
  actor dest = self->spawn(sink) * self->spawn(forward_stage);
  printf("expect %d number of inputs at sink\n",
         (int) cfg.num_sources * cfg.num_msgs);
  for (size_t i = 0; i < cfg.num_sources; ++i)
    self->spawn(source, dest, cfg.num_msgs);
}

} // namespace <anonymous>

CAF_MAIN()
