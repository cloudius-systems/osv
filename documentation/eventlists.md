Event Lists
===========

Event lists let the user or the system, create named events and users to 
register for notification when the events are invoked. This is provided by 
the eventman interface:

1. Creating an Event List

  ```c++
  event_manager->create_event("event_a"); 
  ```

2. Registering and Deregistering Handlers

  ```c++
  int h1 = event_manager->register_event("event_a", [&] { handler1(); });
  int h2 = event_manager->register_event("event_a", [&] { handler2(); });
  event_manager->deregister_event("event_a", h1);
  ```

3. Invoking an Event

  ```c++
  event_manager->invoke_event("event_a");
  ```

  Registered callbacks are dispached with no particular order. The callbacks are exeuted in the context of the thread who invoked the event.
