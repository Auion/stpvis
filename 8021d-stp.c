#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "8021d-stp.h"
#include "device.h"

/***************************************************************************
*  CODE                   
**************************************************************************/

void set_port_state(STP_state *state, Int port_no, State port_state)
{
  state->port_info[port_no].state = port_state;
}

void start_forward_delay_timer(STP_state *state, Int port_no) {
  state->forward_delay_timer[port_no].value = Zero;
  state->forward_delay_timer[port_no].active = True;
}
void stop_message_age_timer(STP_state *state, Int port_no) {
  state->message_age_timer[port_no].active = False;
}

void make_forwarding(STP_state *state, Int port_no)                                   /* (8.6.12)       */
{
   if (state->port_info[port_no].state == Blocking)               /* (8.6.12.3)     */
   {
      set_port_state(state, port_no, Listening);                  /* (8.6.12.3 a)   */
      start_forward_delay_timer(state, port_no);                  /* (8.6.12.3 b)   */
   }
}

Boolean designated_port(STP_state *state, Int port_no) {                                           
 return ((state->port_info[port_no].designated_bridge == state->bridge_info.bridge_id)
           && (state->port_info[port_no].designated_port == state->port_info[port_no].port_id));
}

Boolean root_bridge(STP_state *state) {
  return(state->bridge_info.designated_root == state->bridge_info.bridge_id);
}

void start_topology_change_timer(STP_state *state)
{  state->topology_change_timer.value = (Time) Zero;
   state->topology_change_timer.active = True;
}

void send_tcn_bpdu(STP_state *state, Int port_no, Tcn_bpdu *bpdu) {
  struct Device *device = state->device;
  struct Connection *con = &device->port_connections[port_no-1];
  if (con->active == False) return;

  struct Frame frame = (struct Frame) {
    .destination_mac = BRIDGE_GROUP_ADDRESS,
    .source_mac = {0}, //todo 
    .data = {0},
    .data_length = (sizeof *bpdu)
  };
  assert((sizeof *bpdu) <= (sizeof frame.data));

  memcpy(&frame.data, bpdu, (sizeof *bpdu));
  connection_enqueue(con, frame);
  // assert(1==0 && "Write the bpdu into a frame here");
}
/*
is a pseudo-implementation-specific routine that transmits
the bpdu on the specified port within the specified time.
*/

void transmit_tcn(STP_state *state)                                             /* (8.6.6)        */
{
   Int port_no = state->bridge_info.root_port;
   state->tcn_bpdu[port_no].type = Tcn_bpdu_type;
   send_tcn_bpdu(state, port_no, &state->tcn_bpdu[state->bridge_info.root_port]);/* (8.6.6.3)     */
}

void start_tcn_timer(STP_state *state) {
  state->tcn_timer.value = (Time) Zero;
  state->tcn_timer.active = True;
}
void stop_tcn_timer(STP_state *state) {
  state->tcn_timer.active = False;
}
Boolean tcn_timer_expired(STP_state *state) {
  if (state->tcn_timer.active && (++state->tcn_timer.value >= state->bridge_info.bridge_hello_time)) {
    state->tcn_timer.active = False;
    return(True);
  };
  return(False);
}

void topology_change_detection(STP_state *state)                                /* (8.6.14)       */
{
   if (root_bridge(state))                                      /* (8.6.14.3 a)   */
   {                  
      state->bridge_info.topology_change = True;                  /* (8.6.14.3 a1)*/
      start_topology_change_timer(state);                       /* (8.6.14.3 a2)*/
   }
   else if (state->bridge_info.topology_change_detected == False) /* (8.6.14.3 b)   */
   {                                           
      transmit_tcn(state);                                      /* (8.6.14.3 b1)*/
      start_tcn_timer(state);                                   /* (8.6.14.3 b2)*/
   }
   state->bridge_info.topology_change_detected = True;            /* (8.6.14.3 c)   */
}

void stop_forward_delay_timer(STP_state *state, Int port_no) {
  state->forward_delay_timer[port_no].active = False;
}

void make_blocking(STP_state *state, Int port_no)                                     /* (8.6.13)       */
{
   if ((state->port_info[port_no].state != Disabled)
    && (state->port_info[port_no].state != Blocking))                                                    /* (8.6.13.3)     */
   {
      if ((state->port_info[port_no].state == Forwarding)
       || (state->port_info[port_no].state == Learning))
      {
         if (state->port_info[port_no].change_detection_enabled == True)
                                                           /* (8.5.5.10)     */
         {
             topology_change_detection(state);                  /* (8.6.13.3 a)   */
         }                                                 /* (8.6.14.2.3)   */
      }
      set_port_state(state, port_no, Blocking);                   /* (8.6.13.3 b)   */
      stop_forward_delay_timer(state, port_no);                   /* (8.6.13.3 c)   */
   }
}

void port_state_selection(STP_state *state) {                                   /* (8.6.11)       */
   for (Int port_no = One; port_no <= No_of_ports; port_no++)
   {
      if (port_no == state->bridge_info.root_port)                /* (8.6.11.3 a)   */
      {
         state->port_info[port_no].config_pending = False;        /* (8.6.11.3 a1)*/
         state->port_info[port_no].topology_change_acknowledge = False;
         make_forwarding(state, port_no);                         /* (8.6.11.3 a2)*/
      }
      else if (designated_port(state, port_no))                   /* (8.6.11.3 b)   */
      {
         stop_message_age_timer(state, port_no);                  /* (8.6.11.3 b1)*/
         make_forwarding(state, port_no);                         /* (8.6.11.3 b2)*/
      }        
      else                                                 /* (8.6.11.3 c)   */
      {
         state->port_info[port_no].config_pending = False;        /* (8.6.11.3 c1)*/
         state->port_info[port_no].topology_change_acknowledge = False;
         make_blocking(state, port_no);                           /* (8.6.11.3 c2)*/
      }
   }
}

void root_selection(STP_state *state)                                            /* (8.6.8)       */
{
   Int root_port;
   Int port_no;
   root_port = No_port;
   for (port_no = One; port_no <= No_of_ports; port_no++)     /* (8.6.8.3.1) */
   {
      if (((!designated_port(state, port_no))
       && (state->port_info[port_no].state != Disabled) && (state->port_info[port_no].designated_root < state->bridge_info.bridge_id))
          &&
            ((root_port == No_port)
              || (state->port_info[port_no].designated_root <  state->port_info[root_port].designated_root)  /* (8.6.8.3.1(a)) */
               || ((state->port_info[port_no].designated_root == state->port_info[root_port].designated_root)
                  &&
                  (((state->port_info[port_no].designated_cost + state->port_info[port_no].path_cost)
                        <
                        (state->port_info[root_port].designated_cost + state->port_info[root_port].path_cost)                        /* (8.6.8.3.1(b)) */
                     )
                     ||
                     (  (  (  state->port_info[port_no].designated_cost
                              + state->port_info[port_no].path_cost
                           )
                           ==
                           (  state->port_info[root_port].designated_cost
                              + state->port_info[root_port].path_cost
                           )
                        )
                        &&
                        (  (  state->port_info[port_no].designated_bridge
                              <  state->port_info[root_port].designated_bridge
                           )                               /* (8.6.8.3.1(c)) */
                           ||
                           (  (  state->port_info[port_no].designated_bridge
                                 ==  state->port_info[root_port].designated_bridge
                              )
                              &&
                              (  (  state->port_info[port_no].designated_port
                                    <  state->port_info[root_port].designated_port
                                 )                         /* (8.6.8.3.1(d)) */
                                 ||
                                 (  (  state->port_info[port_no].designated_port
                                       == state->port_info[root_port].designated_port
                                    )
                                    &&
                                    (  state->port_info[port_no].port_id
                                       <  state->port_info[root_port].port_id
                                    )                      /* (8.6.8.3.1(e)) */
         )  )  )  )  )  )  )  )  )
      {
         root_port = port_no;
      }
   }
   state->bridge_info.root_port = root_port;                      /* (8.6.8.3.1)    */
   if (root_port == No_port)                               /* (8.6.8.3.2)    */
   {
      state->bridge_info.designated_root = state->bridge_info.bridge_id;
                                                           /* (8.6.8.3.2(a)) */
      state->bridge_info.root_path_cost = Zero;                   /* (8.6.8.3.2(b)) */
   }
   else                                                    /* (8.6.8.3.3)    */
   {
      state->bridge_info.designated_root = state->port_info[root_port].designated_root;
                                                           /* (8.6.8.3.3(a)) */
      state->bridge_info.root_path_cost = (  state->port_info[root_port].designated_cost
                                      + state->port_info[root_port].path_cost
                                   );                      /* (8.6.8.3.3(b)) */
   }
}

void become_designated_port(STP_state *state, Int port_no)                            /* (8.6.10)       */
{
    state->port_info[port_no].designated_root = state->bridge_info.designated_root; /* (8.6.10.3 a)   */
    state->port_info[port_no].designated_cost = state->bridge_info.root_path_cost; /* (8.6.10.3 b)   */
    state->port_info[port_no].designated_bridge = state->bridge_info.bridge_id; /* (8.6.10.3 c)   */
    state->port_info[port_no].designated_port = state->port_info[port_no].port_id; /* (8.6.10.3 d)   */
}

void designated_port_selection(STP_state *state)                                /* (8.6.9)        */
{
   Int port_no;
   for (port_no = One; port_no <= No_of_ports; port_no++)  /* (8.6.9.3)      */
   {
      if (  designated_port(state, port_no)                       /* (8.6.9.3 a)    */
            ||
            (
               state->port_info[port_no].designated_root
               != state->bridge_info.designated_root              /* (8.6.9.3 b)    */
            )
            ||
            (  state->bridge_info.root_path_cost
               <  state->port_info[port_no].designated_cost
            )                                              /* (8.6.9.3 c)    */
            ||
            (  (  state->bridge_info.root_path_cost
                  == state->port_info[port_no].designated_cost
               )              
               &&
               (  (  state->bridge_info.bridge_id
                     <  state->port_info[port_no].designated_bridge
                  )                                        /* (8.6.9.3 d)    */
                  ||
                  (  (  state->bridge_info.bridge_id
                              == state->port_info[port_no].designated_bridge
                     )
                     &&
                     (  state->port_info[port_no].port_id
                        <=  state->port_info[port_no].designated_port
                     )                                     /* (8.6.9.3 e)    */
         )  )  )  )
      {
         become_designated_port(state, port_no);                  /* (8.6.10.2 a) */
      }
   }
}


void configuration_update(STP_state *state) {                                     /* (8.6.7)        */
   root_selection(state);                                       /* (8.6.7.3.1)    */
                                                           /* (8.6.8.2)      */
   designated_port_selection(state);                            /* (8.6.7.3.2)    */
                                                           /* (8.6.9.2)      */
}



void set_path_cost(STP_state *state, Int port_no, Cost path_cost)                          /* (8.8.6)        */
{
   state->port_info[port_no].path_cost = path_cost;               /* (8.8.6 a)      */
   configuration_update(state);                                 /* (8.8.6 b)      */
   port_state_selection(state);                                 /* (8.8.6 c)      */
}


void enable_change_detection(STP_state *state, Int port_no)                           /* (8.8.7)        */
{
   state->port_info[port_no].change_detection_enabled = True;
}


void disable_change_detection(STP_state *state, Int port_no)                          /* (8.8.8)        */
{
   state->port_info[port_no].change_detection_enabled = False;
}

void start_hello_timer(STP_state *state) {
  state->hello_timer.value = (Time) Zero;
  state->hello_timer.active = True;
}
void stop_hello_timer(STP_state *state) {
  state->hello_timer.active = False;
}
Boolean hello_timer_expired(STP_state *state) {
  if (state->hello_timer.active && (++state->hello_timer.value >= state->bridge_info.hello_time)) { 
    state->hello_timer.active = False;
    return(True);
  };
  return(False);
}


void stop_topology_change_timer(STP_state *state) {
  state->topology_change_timer.active = False;
}
Boolean topology_change_timer_expired(STP_state *state) {
  if (state->topology_change_timer.active
    && ( ++state->topology_change_timer.value >= state->bridge_info.topology_change_time))
  {
    state->topology_change_timer.active = False;
    return(True);
  }
  return(False);
}
void start_message_age_timer(STP_state *state, Int port_no, Time message_age)
{
  state->message_age_timer[port_no].value = message_age;
  state->message_age_timer[port_no].active = True;
}
Boolean message_age_timer_expired(STP_state *state, Int port_no)
{  if (state->message_age_timer[port_no].active &&
         (++state->message_age_timer[port_no].value >= state->bridge_info.max_age))
   {  state->message_age_timer[port_no].active = False;
      return(True);
   }
      return(False);
}
Boolean forward_delay_timer_expired(STP_state *state, Int port_no)
{  if (state->forward_delay_timer[port_no].active &&
       (++state->forward_delay_timer[port_no].value >= state->bridge_info.forward_delay))
   {   state->forward_delay_timer[port_no].active = False;
       return(True);
   }
   return(False);
}
void start_hold_timer(STP_state *state, Int port_no) {
  state->hold_timer[port_no].value = Zero;
  state->hold_timer[port_no].active = True;
}
void stop_hold_timer(STP_state *state, Int port_no)
{
  state->hold_timer[port_no].active = False;
}
Boolean hold_timer_expired(STP_state *state, Int port_no)
{  if (state->hold_timer[port_no].active &&
       (++state->hold_timer[port_no].value >= state->bridge_info.hold_time))
   {
    state->hold_timer[port_no].active = False;
    return(True);
   }
   return(False);
}

/** Elements of Procedure (8.6) **/
/* where */
void send_config_bpdu(STP_state *state, Int port_no, Config_bpdu *bpdu) {
  struct Device *device = state->device;
  struct Connection *con = &device->port_connections[port_no-1];
  if (con->active == False) return;

  struct Frame frame = (struct Frame) {
    .destination_mac = BRIDGE_GROUP_ADDRESS,
    .source_mac = device->mac, //todo 
    .data = {0},
    .data_length = (sizeof *bpdu)
  };
  assert((sizeof *bpdu) <= (sizeof frame.data));

  memcpy(&frame.data, bpdu, (sizeof *bpdu));
  connection_enqueue(con, frame);
  
  // assert(1==0 && "Write the bpdu into a frame here");
};
/*
is a pseudo-implementation specific routine that transmits
the bpdu on the specified port within the specified time.
*/
void transmit_config(STP_state *state, Int port_no) {                                   /* (8.6.1)        */
  if (state->hold_timer[port_no].active) {                      /* (8.6.1.3.1)    */
      state->port_info[port_no].config_pending = True;            /* (8.6.1.3.1)    */
  } else {                                                    /* (8.6.1.3.2)    */
    state->config_bpdu[port_no].type = Config_bpdu_type;
    state->config_bpdu[port_no].root_id = state->bridge_info.designated_root;
                                                         /* (8.6.1.3.2(a))*/
    state->config_bpdu[port_no].root_path_cost = state->bridge_info.root_path_cost;
                                                         /* (8.6.1.3.2(b))*/
    state->config_bpdu[port_no].bridge_id = state->bridge_info.bridge_id;
                                                         /* (8.6.1.3.2(c))*/
    state->config_bpdu[port_no].port_id = state->port_info[port_no].port_id;
  if (root_bridge(state)) {
    state->config_bpdu[port_no].message_age = Zero;          /* (8.6.1.3.2(e))*/
  } else {
         state->config_bpdu[port_no].message_age
           =  state->message_age_timer[state->bridge_info.root_port].value
              + Message_age_increment;                     /* (8.6.1.3.2(f))*/
      }
      state->config_bpdu[port_no].max_age = state->bridge_info.max_age;  /* (8.6.1.3.2(g))*/
      state->config_bpdu[port_no].hello_time = state->bridge_info.hello_time;
      state->config_bpdu[port_no].forward_delay = state->bridge_info.forward_delay;
      state->config_bpdu[port_no].topology_change_acknowledgment
        =  state->port_info[port_no].topology_change_acknowledge;
                                                           /* (8.6.1.3.2(h)) */
      state->config_bpdu[port_no].topology_change
        = state->bridge_info.topology_change;                     /* (8.6.1.3.2(i)) */
      if (state->config_bpdu[port_no].message_age < state->bridge_info.max_age)
      {
         state->port_info[port_no].topology_change_acknowledge = False;
                                                              /* (8.6.1.3.3) */
         state->port_info[port_no].config_pending = False;           /* (8.6.1.3.3)*/
         send_config_bpdu(state, port_no, &state->config_bpdu[port_no]);
         start_hold_timer(state, port_no);                           /* (8.6.3.3(b))*/
       }
   }
}

void config_bpdu_generation(STP_state *state)                                   /* (8.6.4)        */
{
   Int port_no;
   for (port_no = One; port_no <= No_of_ports; port_no++)  /* (8.6.4.3)      */
   {
      if (  designated_port(state, port_no)                       /* (8.6.4.3)      */
            &&
            (state->port_info[port_no].state != Disabled)
         )
      {
         transmit_config(state, port_no);                         /* (8.6.4.3)      */
      }                                                    /* (8.6.1.2)      */
   }
}

void hello_timer_expiry(STP_state *state)                                       /* (8.7.3)        */
{
   config_bpdu_generation(state);                               /* (8.6.4.2 b)    */
   start_hello_timer(state);
}

void tcn_timer_expiry(STP_state *state)                                         /* (8.7.6)        */
{
  transmit_tcn(state);                                      /* (8.7.6 a)      */
  start_tcn_timer(state);                                   /* (8.7.6 b)      */
}

void topology_change_timer_expiry(STP_state *state)                             /* (8.7.7)        */
{
  state->bridge_info.topology_change_detected = False;        /* (8.7.7 a)      */
  state->bridge_info.topology_change = False;                 /* (8.7.7 b)      */
}

void message_age_timer_expiry(STP_state *state, Int port_no)                          /* (8.7.4)        */
{                                      
   Boolean root;
   root = root_bridge(state);
   become_designated_port(state, port_no);                        /* (8.7.4 a)      */
                                                           /* (8.6.10.2 b)   */
   configuration_update(state);                                 /* (8.7.4 b)      */
                                                           /* (8.6.7.2 b)    */
   port_state_selection(state);                                 /* (8.7.4 c)      */
                                                           /* (8.6.11.2 b)   */
   if ((root_bridge(state)) && (!root))                         /* (8.7.4 d)      */
   {
      state->bridge_info.max_age = state->bridge_info.bridge_max_age;    /* (8.7.4 d1)    */
      state->bridge_info.hello_time = state->bridge_info.bridge_hello_time;
      state->bridge_info.forward_delay = state->bridge_info.bridge_forward_delay;
      topology_change_detection(state);                         /* (8.7.4 d2)    */
                                                           /* (8.6.14.2.4)   */
      stop_tcn_timer(state);                                    /* (8.7.4 d3)    */
      config_bpdu_generation(state);                            /* (8.7.4 d4)    */
      start_hello_timer(state);
   }
}


Boolean designated_for_some_port(STP_state *state)
{
  for (Int port_no = One; port_no <= No_of_ports; port_no++) {
    if ( state->port_info[port_no].designated_bridge == state->bridge_info.bridge_id) {
      return(True);
    };
  };
  return(False);
}

void forward_delay_timer_expiry(STP_state *state, Int port_no)                        /* (8.7.5)        */
{
   if (state->port_info[port_no].state == Listening)              /* (8.7.5 a)      */
   {   
      set_port_state(state, port_no, Learning);                   /* (8.7.5 a1)    */
      start_forward_delay_timer(state, port_no);                  /* (8.7.5 a2)    */
   }
   else if (state->port_info[port_no].state == Learning)          /* (8.7.5 b)      */
   {   
      set_port_state(state, port_no, Forwarding);                 /* (8.7.5 b1)    */
      if (designated_for_some_port(state))                      /* (8.7.5 b2)    */
      {
         if (state->port_info[port_no].change_detection_enabled == True)
                                                           /* (8.5.5.10)     */
         {
             topology_change_detection(state);                  /* (8.6.14.2.2)   */
         }
      }
   }
}

void hold_timer_expiry(STP_state *state, Int port_no)                                 /* (8.7.8)        */
{
  if (state->port_info[port_no].config_pending) {
    transmit_config(state, port_no);                            /* (8.6.1.2)      */
  };
}

/** pseudo-implementation-specific timer running support **/   
// Run this function 256 times per second?
void tick(STP_state *state) {
   Int port_no;
          
   if (hello_timer_expired(state)) {
    hello_timer_expiry(state);
   };
   if (tcn_timer_expired(state)) {
     tcn_timer_expiry(state);
   };
   if (topology_change_timer_expired(state)) {
      topology_change_timer_expiry(state);
   };
   for (port_no = One; port_no <= No_of_ports; port_no++) {
      if (message_age_timer_expired(state, port_no)) {
         message_age_timer_expiry(state, port_no);
      }
   }
   for (port_no = One; port_no <= No_of_ports; port_no++) {
      if (forward_delay_timer_expired(state, port_no)) {
         forward_delay_timer_expiry(state, port_no);
      }
      if (hold_timer_expired(state, port_no)) {
         hold_timer_expiry(state, port_no);
      }
   }
}



Boolean supersedes_port_info(STP_state *state, Int port_no, Config_bpdu *config)              /* (8.6.2.2)      */
{  
   return (
   (  config->root_id
      <  state->port_info[port_no].designated_root                /* (8.6.2.2 a)    */
   )
   ||
   (  (  config->root_id
         == state->port_info[port_no].designated_root
      )
      &&
      (  (  config->root_path_cost
            <  state->port_info[port_no].designated_cost          /* (8.6.2.2 b)    */
         )
         ||
       (  (  config->root_path_cost
               == state->port_info[port_no].designated_cost
            )
            &&
            (  (  config->bridge_id
                  <  state->port_info[port_no].designated_bridge  /* (8.6.2.2 c)    */
               )
               ||
               (  (  config->bridge_id
                     == state->port_info[port_no].designated_bridge
                  )                                        /* (8.6.2.2 d)    */
                  &&
                  (  (  config->bridge_id != state->bridge_info.bridge_id
                     )                                     /* (8.6.2.2 d1) */
                     ||
                     (  config->port_id
                        <= state->port_info[port_no].designated_port
                     )                                     /* (8.6.2.2 d2) */
   )  )  )  )  )  )
          );
}


void record_config_information(STP_state *state, Int port_no, Config_bpdu *config)                  /* (8.6.2)       */
{
   state->port_info[port_no].designated_root = config->root_id;    /* (8.6.2.3.1)   */
   state->port_info[port_no].designated_cost = config->root_path_cost;
   state->port_info[port_no].designated_bridge = config->bridge_id;
   state->port_info[port_no].designated_port = config->port_id;
   start_message_age_timer(state, port_no, config->message_age);   /* (8.6.2.3.2)   */
}

void record_config_timeout_values(STP_state *state, Config_bpdu *config)                       /* (8.6.3)        */
{
   state->bridge_info.max_age = config->max_age;                  /* (8.6.3.3)      */
   state->bridge_info.hello_time = config->hello_time;
   state->bridge_info.forward_delay = config->forward_delay;
   state->bridge_info.topology_change = config->topology_change;
}

void reply(STP_state *state, Int port_no)                                             /* (8.6.5)        */
{
   transmit_config(state, port_no);                               /* (8.6.5.3)      */
}
               
void topology_change_acknowledged(STP_state *state)                              /* (8.6.15)      */
{
   state->bridge_info.topology_change_detected = False;            /* (8.6.15.3 a)  */
   stop_tcn_timer(state);                                        /* (8.6.15.3 b)  */
}

void acknowledge_topology_change(STP_state *state, Int port_no)                        /* (8.6.16)      */
{
   state->port_info[port_no].topology_change_acknowledge = True;   /* (8.6.16.3 a)  */
   transmit_config(state, port_no);                                /* (8.6.16.3 b)  */
}

/** Operation of the Protocol (8.7) **/
void received_config_bpdu(STP_state *state, Int port_no, Config_bpdu *config)                      /* (8.7.1)        */
{
   Boolean root;
   root = root_bridge(state);
   if (state->port_info[port_no].state != Disabled)               
   {
      if (supersedes_port_info(state, port_no, config))           /* (8.7.1.1)      */
      {                                                    /* (8.6.2.2)      */
         record_config_information(state, port_no, config);       /* (8.7.1.1 a)    */
                                                           /* (8.6.2.2)      */
         configuration_update(state);                           /* (8.7.1.1 b)    */
                                                          /* (8.6.7.2 a)    */
         port_state_selection(state);                           /* (8.7.1.1 c)    */
                                                           /* (8.6.11.2 a)   */
         if ((!root_bridge(state)) && root)                     /* (8.7.1.1 d)    */
         {
            stop_hello_timer(state);
            if (state->bridge_info.topology_change_detected)      /* (8.7.1.1 e)    */
            {
               stop_topology_change_timer(state);
               transmit_tcn(state);                             /* (8.6.6.1)      */
               start_tcn_timer(state);
            }
         }
         if (port_no == state->bridge_info.root_port)
         {
            record_config_timeout_values(state, config);          /* (8.7.1.1 e)    */
                                                           /* (8.6.3.2)      */
            config_bpdu_generation(state);                      /* (8.6.4.2 a)    */
            if (config->topology_change_acknowledgment)    /* (8.7.1.1 g)    */
            {
               topology_change_acknowledged(state);             /* (8.6.15.2)     */
            }
         }                                                                     
      }
      else if (designated_port(state, port_no))                   /* (8.7.1.2)      */
      {
         reply(state, port_no);                                   /* (8.7.1.2)    */
                                                           /* (8.6.5.2)      */
      }
   }
}


void received_tcn_bpdu(STP_state *state, Int port_no, Tcn_bpdu tcn)                            /* (8.7.2)        */
{
   if (state->port_info[port_no].state != Disabled)               
   {
      if (designated_port(state, port_no))
      {
         topology_change_detection(state);                      /* (8.7.2 a)      */
                                                           /* (8.6.14.2.1)   */
         acknowledge_topology_change(state, port_no);             /* (8.7.2 b)      */
      }                                                    /* (8.6.16.2)     */
   }
}              


void initialize_port(STP_state *state, Int port_no) {
  become_designated_port(state, port_no);                     /* (8.8.1 d1)    */
  set_port_state(state, port_no, Blocking);                   /* (8.8.1 d2)    */
  state->port_info[port_no].topology_change_acknowledge = False;
                                                       /* (8.8.1 d3)    */
  
  state->port_info[port_no].config_pending = False;           /* (8.8.1 d4)    */
  state->port_info[port_no].change_detection_enabled = True;  /* (8.8.1 d8)    */
  stop_message_age_timer(state, port_no);                     /* (8.8.1 d5)    */
  stop_forward_delay_timer(state, port_no);                   /* (8.8.1 d6)    */
  stop_hold_timer(state, port_no);                            /* (8.8.1 d7)    */
}

/** Management of the Bridge Protocol Entity (8.8) **/
void stp_initialisation(STP_state *state)                                           /* (8.8.1)        */
{
   state->bridge_info.designated_root = state->bridge_info.bridge_id;    /* (8.8.1 a)      */
   state->bridge_info.root_path_cost = Zero;
   state->bridge_info.root_port = No_port;
   state->bridge_info.max_age = state->bridge_info.bridge_max_age;       /* (8.8.1 b)      */
   state->bridge_info.hello_time = state->bridge_info.bridge_hello_time;
   state->bridge_info.forward_delay = state->bridge_info.bridge_forward_delay;
                                                          
   state->bridge_info.topology_change_detected = False;           /* (8.8.1 c)      */
   state->bridge_info.topology_change = False;
   stop_tcn_timer(state);
   stop_topology_change_timer(state);
   for (Int port_no = One; port_no <= No_of_ports; port_no++)  /* (8.8.1 d)      */
   {                                                      
      initialize_port(state, port_no);
   }
   port_state_selection(state);                                 /* (8.8.1 e)      */
   config_bpdu_generation(state);                             /* (8.8.1 f)      */
   start_hello_timer(state);
}


void enable_port(STP_state *state, Int port_no)                                       /* (8.8.2)        */
{
   initialize_port(state, port_no);
   port_state_selection(state);                                 /* (8.8.2 g)      */
}


void disable_port(STP_state *state, Int port_no)                                      /* (8.8.3)       */
{
   Boolean root;
   root = root_bridge(state);
   become_designated_port(state, port_no);                        /* (8.8.3 a)      */
       
   set_port_state(state, port_no, Disabled);                      /* (8.8.3 b)      */
   state->port_info[port_no].topology_change_acknowledge = False; /* (8.8.3 c)      */
      
   state->port_info[port_no].config_pending = False;              /* (8.8.3 d)      */
   stop_message_age_timer(state, port_no);                        /* (8.8.3 e)      */
   stop_forward_delay_timer(state, port_no);                      /* (8.8.3 f)      */
   configuration_update(state);                                 /* (8.8.3 g)      */
   port_state_selection(state);                                 /* (8.8.3 h)      */
   if ((root_bridge(state)) && (!root))                         /* (8.8.3 i)      */
   {
      state->bridge_info.max_age = state->bridge_info.bridge_max_age;    /* (8.8.3 i1)    */
      state->bridge_info.hello_time = state->bridge_info.bridge_hello_time;
      state->bridge_info.forward_delay = state->bridge_info.bridge_forward_delay;
      topology_change_detection(state);                         /* (8.8.3 i2)    */
      stop_tcn_timer(state);                                    /* (8.8.3 i3)    */
      config_bpdu_generation(state);                            /* (8.8.3 i4)    */
      start_hello_timer(state);
   }
}

// Use this function to smuggle in the device MAC address.
// The MAC for the bridge MAY be the same as a port (perferrably port 1) (7.12.5)
void set_bridge_priority(STP_state *state, Identifier new_bridge_id)                         /* (8.8.4)        */
{
   Boolean root;
   Int port_no;
   root = root_bridge(state);
   for (port_no = One; port_no <= No_of_ports; port_no++)  /* (8.8.4 b)      */
   {
      if (designated_port(state, port_no))
      {
         state->port_info[port_no].designated_bridge = new_bridge_id;
      }
   }
   state->bridge_info.bridge_id = new_bridge_id;                  /* (8.8.4 c)      */
   configuration_update(state);                                 /* (8.8.4 d)      */
   port_state_selection(state);                                 /* (8.8.4 e)      */
   if ((root_bridge(state)) && (!root))                         /* (8.8.4 f)      */
   {
      state->bridge_info.max_age = state->bridge_info.bridge_max_age;    /* (8.8.4 f1)    */
      state->bridge_info.hello_time = state->bridge_info.bridge_hello_time;
      state->bridge_info.forward_delay = state->bridge_info.bridge_forward_delay;
      topology_change_detection(state);                         /* (8.8.4 f2)    */
      stop_tcn_timer(state);                                    /* (8.8.4 f3)    */
      config_bpdu_generation(state);                            /* (8.8.4 f4)    */
      start_hello_timer(state);
   }
}

// Use this function to smuggle in the port's MAC address
void set_port_priority(STP_state *state, Int port_no, Port_id new_port_id)                    /* (8.8.5)        */
{
   if (designated_port(state, port_no))                           /* (8.8.5 b)      */
   {
      state->port_info[port_no].designated_port = new_port_id;
   }
   state->port_info[port_no].port_id = new_port_id;               /* (8.8.5 c)      */
   if ( (  state->bridge_info.bridge_id                           /* (8.8.5 d)      */
            == state->port_info[port_no].designated_bridge
        )
        &&
        (  state->port_info[port_no].port_id
           <  state->port_info[port_no].designated_port
        )
      )
   {
      become_designated_port(state, port_no);                     /* (8.8.5 d1)   */
      port_state_selection(state);                              /* (8.8.5 d2)   */
   }
}



