#include <cassert>
#include <iostream>
#include <string>

#include <BuddyCore.h>

int main() {
  const buddy::LocalTime time{2026, 9, 25, 5, 14, 30, 0};
  buddy::BuddyCore core;
  core.begin(0);
  int wakes = 0;
  core.setEventCallback([](const buddy::Event& event, void* context) {
    if (event.type == buddy::EventType::WakeDetected) {
      assert(event.id == "hey");
      ++*static_cast<int*>(context);
    }
  }, &wakes);

  assert(core.hear("What is the time?", 100, time) == "ignored");
  assert(core.hear("They asked a question", 200, time) == "ignored");
  assert(core.hear("Please say hey to me", 300, time) == "ignored");
  assert(core.hear("Hey, what is the time?", 400, time) == "handled");
  assert(core.snapshot().detail.find("14:30") != std::string::npos);
  assert(core.hear("Hey, tell me a story", 500, time) == "assistant");
  assert(core.assistantQuery() == "tell me a story");
  assert(core.hear("Hey!", 600, time) == "awake");
  assert(core.hear("How are you?", 700, time) == "assistant");
  assert(core.assistantQuery() == "how are you");
  assert(core.hear("How are you?", 800, time) == "ignored");
  assert(core.hear("Hey", 900, time) == "awake");
  assert(core.hear("How are you?", 12901, time) == "ignored");
  assert(wakes == 4);
  std::cout << "Fixed Hey wake word and assistant routing: OK\n";
}
