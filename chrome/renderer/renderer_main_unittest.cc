// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "base/process_util.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/main_function_params.h"
#include "ipc/ipc_channel.h"
#include "ipc/ipc_switches.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

// TODO(port): Bring up this test this on other platforms.
#if defined(OS_POSIX)

using base::ProcessHandle;

const char kRendererTestChannelName[] = "test";

extern int RendererMain(const MainFunctionParams& parameters);

// TODO(port): The IPC Channel tests have a class with extremely similar
// functionality, we should combine them.
class RendererMainTest : public base::MultiProcessTest {
 protected:
  RendererMainTest() {}

  // Create a new MessageLoopForIO For each test.
  virtual void SetUp();
  virtual void TearDown();

  // Spawns a child process of the specified type
  base::ProcessHandle SpawnChild(const std::string& procname,
                                 IPC::Channel* channel);

  // Created around each test instantiation.
  MessageLoopForIO *message_loop_;
};

void RendererMainTest::SetUp() {
  MultiProcessTest::SetUp();

  // Construct a fresh IO Message loop for the duration of each test.
  message_loop_ = new MessageLoopForIO();
}

void RendererMainTest::TearDown() {
  delete message_loop_;
  message_loop_ = NULL;

  MultiProcessTest::TearDown();
}

ProcessHandle RendererMainTest::SpawnChild(const std::string& procname,
                                           IPC::Channel* channel) {
  base::file_handle_mapping_vector fds_to_map;
  const int ipcfd = channel->GetClientFileDescriptor();
  if (ipcfd > -1) {
    fds_to_map.push_back(std::pair<int,int>(ipcfd, 3));
  }

  return MultiProcessTest::SpawnChild(procname, fds_to_map, false);
}

// Listener class that kills the message loop when it connects.
class SuicidalListener : public IPC::Channel::Listener {
 public:
  explicit SuicidalListener(bool* suicide_success)
      : suicide_success_(suicide_success) {}

  void OnChannelConnected(int32 peer_pid) {
    *suicide_success_ = true;
    MessageLoop::current()->Quit();
  }

  void OnMessageReceived(const IPC::Message& message) {
    // We shouldn't receive any messages
    ASSERT_TRUE(false);
  }

  // A flag that we set when we have successfully suicided.
  bool* suicide_success_;
};

MULTIPROCESS_TEST_MAIN(SimpleRenderer) {
  SandboxInitWrapper dummy_sandbox_init;
  CommandLine cl(*CommandLine::ForCurrentProcess());
  cl.AppendSwitchASCII(switches::kProcessChannelID, kRendererTestChannelName);

  MainFunctionParams dummy_params(cl, dummy_sandbox_init, NULL);
  return RendererMain(dummy_params);
}

TEST_F(RendererMainTest, CreateDestroy) {
  bool suicide_success = false;
  SuicidalListener listener(&suicide_success);
  IPC::Channel control_channel(kRendererTestChannelName,
                               IPC::Channel::MODE_SERVER,
                               &listener);
  base::ProcessHandle renderer_pid = SpawnChild("SimpleRenderer",
                                                &control_channel);

  ASSERT_TRUE(control_channel.Connect());

  // The SuicidalListener *should* cause the following MessageLoop run
  // to quit, but just in case it doesn't, insert a QuitTask at the back
  // of the queue.
  MessageLoop::current()->PostDelayedTask(FROM_HERE, new MessageLoop::QuitTask,
                                          TestTimeouts::action_timeout_ms());
  MessageLoop::current()->Run();
  ASSERT_TRUE(suicide_success);

  // The renderer should exit when we close the channel.
  control_channel.Close();

  EXPECT_TRUE(base::WaitForSingleProcess(renderer_pid,
                                         TestTimeouts::action_timeout_ms()));
}
#endif  // defined(OS_POSIX)
