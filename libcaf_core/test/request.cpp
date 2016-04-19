/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2015                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/config.hpp"

#define CAF_SUITE request
#include "caf/test/unit_test.hpp"

#include "caf/all.hpp"

using namespace std;
using namespace caf;

using std::chrono::milliseconds;

namespace {

using f_atom = atom_constant<atom("f")>;
using i_atom = atom_constant<atom("i")>;
using idle_atom = atom_constant<atom("idle")>;
using error_atom = atom_constant<atom("error")>;
using request_atom = atom_constant<atom("request")>;
using response_atom = atom_constant<atom("response")>;
using go_atom = atom_constant<atom("go")>;
using gogo_atom = atom_constant<atom("gogo")>;
using gogogo_atom = atom_constant<atom("gogogo")>;
using no_way_atom = atom_constant<atom("NoWay")>;
using hi_there_atom = atom_constant<atom("HiThere")>;

struct sync_mirror : event_based_actor {
  sync_mirror(actor_config& cfg) : event_based_actor(cfg) {
    // nop
  }

  behavior make_behavior() override {
    set_unexpected_handler(reflect_unexpected);
    return {
      [] {
        // nop
      }
    };
  }
};

// replies to 'f' with 0.0f and to 'i' with 0
struct float_or_int : event_based_actor {
  float_or_int(actor_config& cfg) : event_based_actor(cfg) {
    // nop
  }

  behavior make_behavior() override {
    return {
      [](f_atom) {
        return 0.0f;
      },
      [](i_atom) {
        return 0;
      }
    };
  }
};

class popular_actor : public event_based_actor { // popular actors have a buddy
public:
  explicit popular_actor(actor_config& cfg, const actor& buddy_arg)
      : event_based_actor(cfg),
        buddy_(buddy_arg) {
    // don't pollute unit test output with (provoked) warnings
    set_unexpected_handler(drop_unexpected);
  }

  inline const actor& buddy() const {
    return buddy_;
  }

private:
  actor buddy_;
};

/******************************************************************************\
 *                                test case 1:                                *
 *                                                                            *
 *                  A                  B                  C                   *
 *                  |                  |                  |                   *
 *                  | --(delegate)---> |                  |                   *
 *                  |                  | --(forward)----> |                   *
 *                  |                  X                  |---\               *
 *                  |                                     |   |               *
 *                  |                                     |<--/               *
 *                  | <-------------(reply)-------------- |                   *
 *                  X                                     X                   *
\******************************************************************************/

class A : public popular_actor {
public:
  explicit A(actor_config& cfg, const actor& buddy_arg)
      : popular_actor(cfg, buddy_arg) {
    // nop
  }

  behavior make_behavior() override {
    return {
      [=](go_atom, const actor& next) {
        return delegate(next, gogo_atom::value);
      }
    };
  }
};

class B : public popular_actor {
public:
  explicit B(actor_config& cfg, const actor& buddy_arg)
      : popular_actor(cfg, buddy_arg) {
    // nop
  }

  behavior make_behavior() override {
    return {
      [=](gogo_atom x) {
        CAF_MESSAGE("forward message to buddy");
        quit();
        return delegate(buddy(), x);
      }
    };
  }
};

class C : public event_based_actor {
public:
  C(actor_config& cfg) : event_based_actor(cfg) {
    // don't pollute unit test output with (provoked) warnings
    set_unexpected_handler(drop_unexpected);
  }

  behavior make_behavior() override {
    return {
      [=](gogo_atom) -> atom_value {
        CAF_MESSAGE("received `gogo_atom`, about to quit");
        quit();
        return ok_atom::value;
      }
    };
  }
};

/******************************************************************************\
 *                                test case 2:                                *
 *                                                                            *
 *                  A                  D                  C                   *
 *                  |                  |                  |                   *
 *                  | ---(request)---> |                  |                   *
 *                  |                  | ---(request)---> |                   *
 *                  |                  |                  |---\               *
 *                  |                  |                  |   |               *
 *                  |                  |                  |<--/               *
 *                  |                  | <---(reply)----- |                   *
 *                  | <---(reply)----- |                                      *
 *                  X                  X                                      *
\******************************************************************************/

class D : public popular_actor {
public:
  explicit D(actor_config& cfg, const actor& buddy_arg)
      : popular_actor(cfg, buddy_arg) {
    // nop
  }

  behavior make_behavior() override {
    return {
      [=](gogo_atom x) -> response_promise {
        auto rp = make_response_promise();
        request(buddy(), infinite, x).then(
          [=](ok_atom x) mutable {
            rp.deliver(x);
            quit();
          }
        );
        return rp;
      }
    };
  }
};

/******************************************************************************\
 *                                test case 3:                                *
 *                                                                            *
 *                Client            Server              Worker                *
 *                  |                  |                  |                   *
 *                  |                  | <---(idle)------ |                   *
 *                  | ---(request)---> |                  |                   *
 *                  |                  | ---(request)---> |                   *
 *                  |                  |                  |---\               *
 *                  |                  X                  |   |               *
 *                  |                                     |<--/               *
 *                  | <------------(response)------------ |                   *
 *                  X                                                         *
\******************************************************************************/

behavior server(event_based_actor* self) {
printf("server id: %d\n", (int) self->id());
  return {
    [=](idle_atom, actor worker) {
      self->become(
        keep_behavior,
        [=](request_atom task) {
          self->unbecome(); // await next idle message
          return self->delegate(worker, task);
        },
        [](idle_atom) {
          return skip_message();
        }
      );
    },
    [](request_atom) {
      return skip_message();
    }
  };
}

struct fixture {
  actor_system system;
  scoped_actor self;
  fixture() : system(), self(system) {
    // nop
  }
};

} // namespace <anonymous>

CAF_TEST_FIXTURE_SCOPE(atom_tests, fixture)

CAF_TEST(test_void_res) {
  using testee_a = typed_actor<replies_to<int, int>::with<void>>;
  auto buddy = system.spawn([]() -> testee_a::behavior_type {
    return [](int, int) {
      // nop
    };
  });
  self->request(buddy, infinite, 1, 2).receive(
    [] {
      CAF_MESSAGE("received void res");
    }
  );
}

CAF_TEST(pending_quit) {
  auto mirror = system.spawn([](event_based_actor* self) -> behavior {
    self->set_unexpected_handler(reflect_unexpected);
    return {
      [] {
        // nop
      }
    };
  });
  system.spawn([mirror](event_based_actor* self) {
    self->request(mirror, infinite, 42).then(
      [](int) {
        CAF_ERROR("received result, should've been terminated already");
      },
      [](const error& err) {
        CAF_CHECK_EQUAL(err, sec::request_receiver_down);
      }
    );
    self->quit();
  });
}

CAF_TEST(request_float_or_int) {
  int invocations = 0;
  auto foi = self->spawn<float_or_int, linked>();
  self->send(foi, i_atom::value);
  self->receive(
    [](int i) {
      CAF_CHECK_EQUAL(i, 0);
    }
  );
  self->request(foi, infinite, i_atom::value).receive(
    [&](int i) {
      CAF_CHECK_EQUAL(i, 0);
      ++invocations;
    },
    [&](const error& err) {
      CAF_ERROR("Error: " << self->system().render(err));
    }
  );
  self->request(foi, infinite, f_atom::value).receive(
    [&](float f) {
      CAF_CHECK_EQUAL(f, 0.f);
      ++invocations;
    },
    [&](const error& err) {
      CAF_ERROR("Error: " << self->system().render(err));
    }
  );
  CAF_CHECK_EQUAL(invocations, 2);
  CAF_MESSAGE("trigger sync failure");
  bool error_handler_called = false;
  bool int_handler_called = false;
  self->request(foi, infinite, f_atom::value).receive(
    [&](int) {
      CAF_ERROR("int handler called");
      int_handler_called = true;
    },
    [&](const error& err) {
      CAF_MESSAGE("error received");
      CAF_CHECK_EQUAL(err, sec::unexpected_response);
      error_handler_called = true;
    }
  );
  CAF_CHECK_EQUAL(error_handler_called, true);
  CAF_CHECK_EQUAL(int_handler_called, false);
}

CAF_TEST(request_to_mirror) {
  auto mirror = system.spawn<sync_mirror>();
  self->request(mirror, infinite, 42).receive([&](int value) {
    CAF_CHECK_EQUAL(value, 42);
  });
}

CAF_TEST(request_to_a_fwd2_b_fwd2_c) {
  scoped_actor self{system};
  self->request(self->spawn<A, monitored>(self), infinite,
                go_atom::value, self->spawn<B>(self->spawn<C>())).receive(
    [](ok_atom) {
      CAF_MESSAGE("received 'ok'");
    }
  );
}

CAF_TEST(request_to_a_fwd2_d_fwd2_c) {
  self->request(self->spawn<A, monitored>(self), infinite,
                go_atom::value, self->spawn<D>(self->spawn<C>())).receive(
    [](ok_atom) {
      CAF_MESSAGE("received 'ok'");
    }
  );
}

CAF_TEST(request_to_self) {
  self->request(self, milliseconds(50), no_way_atom::value).receive(
    [&] {
      CAF_ERROR("unexpected empty message");
    },
    [&](const error& err) {
      CAF_MESSAGE("err = " << system.render(err));
      CAF_REQUIRE(err == sec::request_timeout);
    }
  );
}

CAF_TEST(invalid_request) {
  self->request(self->spawn<C>(), milliseconds(500),
                hi_there_atom::value).receive(
    [&](hi_there_atom) {
      CAF_ERROR("C did reply to 'HiThere'");
    },
    [&](const error& err) {
      CAF_REQUIRE(err == sec::unexpected_message);
    }
  );
}

CAF_TEST(client_server_worker_user_case) {
  auto serv = self->spawn<linked>(server);                       // server
  auto work = self->spawn<linked>([]() -> behavior {             // worker
    return {
      [](request_atom) {
        return response_atom::value;
      }
    };
  });
  // first 'idle', then 'request'
  anon_send(serv, idle_atom::value, work);
  self->request(serv, infinite, request_atom::value).receive(
    [&](response_atom) {
      CAF_MESSAGE("received 'response'");
      CAF_CHECK_EQUAL(self->current_sender(), work.address());
    },
    [&](const error& err) {
      CAF_ERROR("error: " << self->system().render(err));
    }
  );
  // first 'request', then 'idle'
  auto handle = self->request(serv, infinite, request_atom::value);
  send_as(work, serv, idle_atom::value, work);
  handle.receive(
    [&](response_atom) {
      CAF_CHECK_EQUAL(self->current_sender(), work.address());
    },
    [&](const error& err) {
      CAF_ERROR("error: " << self->system().render(err));
    }
  );
}

behavior snyc_send_no_then_A(event_based_actor * self) {
  return [=](int number) {
    CAF_MESSAGE("got " << number);
    self->quit();
  };
}

behavior snyc_send_no_then_B(event_based_actor * self) {
  return {
    [=](int number) {
      self->request(self->spawn(snyc_send_no_then_A), infinite, number);
      self->quit();
    }
  };
}

CAF_TEST(request_no_then) {
  anon_send(system.spawn(snyc_send_no_then_B), 8);
}

CAF_TEST(async_request) {
  auto foo = system.spawn([](event_based_actor* self) -> behavior {
    auto receiver = self->spawn<linked>([](event_based_actor* self) -> behavior{
      return {
        [=](int) {
          return self->make_response_promise();
        }
      };
    });
    self->request(receiver, infinite, 1).then(
      [=](int) {}
    );
    return {
      [=](int) {
        CAF_MESSAGE("int received");
        self->quit(exit_reason::user_shutdown);
      }
    };
  });
  anon_send(foo, 1);
}

CAF_TEST_FIXTURE_SCOPE_END()