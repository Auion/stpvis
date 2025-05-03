
/***************************************************************************
*
*  SPANNING TREE ALGORITHM AND PROTOCOL
*
**************************************************************************/
/***************************************************************************
*  DEFINED CONSTANTS
**************************************************************************/
#ifndef IEEE8021D_STP_H
#define IEEE8021D_STP_H

#define Zero        0
#define One         1
#define False       0
#define True        1
/** port states. **/
#define Disabled       0                                   /* (8.4.5)        */
#define Listening      1                                   /* (8.4.2)        */
#define Learning       2                                   /* (8.4.3)        */
#define Forwarding     3                                   /* (8.4.4)        */
#define Blocking       4                                   /* (8.4.1)        */
/** BPDU type constants **/
 
#define Config_bpdu_type     0
#define Tcn_bpdu_type      128
/** pseudo-implementation constants. **/
#define No_of_ports 12
        /* arbitrary choice, to allow the code below to compile */
#define All_ports No_of_ports+1
        /* ports start at 1, arrays in C start at 0 */ 
#define Default_path_cost 10
        /* arbitrary */
#define Message_age_increment 1
        /* minimum increment possible to avoid underestimating age, allows
           for BPDU transmission time */
#define No_port 0
        /* reserved value for Bridge's root port parameter indicating no
           root port, used when Bridge is the root */
/***************************************************************************
*  TYPEDEFS, STRUCTURES, AND UNION DECLARATIONS
**************************************************************************/
/** basic types. **/
typedef int Int;        /* to align with convention used here for use of
                           case. Types and defined constants have their
                           initial letters capitalized. */
typedef Int Boolean;    /* : (True, False) */
typedef Int State;      /* : (Disabled, Listening, Learning,
                              Forwarding, Blocking) */
/** BPDU encoding types defined in Clause 9, “Encoding of Bridge Protocol
Data Units” are:
Protocol_version     (9.2.2)
Bpdu_type            (9.2.3)
Flag                 (9.2.4)
Identifier           (9.2.5)
Cost                 (9.2.6)
Port_id              (9.2.7)
Time                 (9.2.8)
**/

typedef unsigned long Identifier;
typedef unsigned short Time;
typedef unsigned int Cost;
typedef short Bpdu_type;
typedef Boolean Flag;
typedef unsigned short Port_id;


/** Configuration BPDU Parameters (8.5.1) **/
typedef struct
{
   Bpdu_type  type;
   Identifier root_id;                                     /* (8.5.1.1)      */
   Cost       root_path_cost;                              /* (8.5.1.2)      */
   Identifier bridge_id;                                   /* (8.5.1.3)      */
   Port_id    port_id;                                     /* (8.5.1.4)      */
   Time       message_age;                                 /* (8.5.1.5)      */
   Time       max_age;                                     /* (8.5.1.6)      */
   Time       hello_time;                                  /* (8.5.1.7)      */
   Time       forward_delay;                               /* (8.5.1.8)      */
   Flag       topology_change_acknowledgment;              /* (8.5.1.9)      */
   Flag       topology_change;                             /* (8.5.1.10)     */
              
} Config_bpdu;


/** Topology Change Notification BPDU Parameters (8.5.2) **/
typedef struct
{
   Bpdu_type  type;
} Tcn_bpdu;
/** Bridge Parameters (8.5.3) **/
typedef struct
{
   Identifier designated_root;                             /* (8.5.3.1)      */
   Cost       root_path_cost;                              /* (8.5.3.2)      */
   Int        root_port;                                   /* (8.5.3.3)      */
   Time       max_age;                                     /* (8.5.3.4)      */
   Time       hello_time;                                  /* (8.5.3.5)      */
   Time       forward_delay;                               /* (8.5.3.6)      */
   Identifier bridge_id;                                   /* (8.5.3.7)      */
   Time       bridge_max_age;                              /* (8.5.3.8)      */
 
   Time       bridge_hello_time;                           /* (8.5.3.9)      */
   Time       bridge_forward_delay;                        /* (8.5.3.10)     */
   Boolean    topology_change_detected;                    /* (8.5.3.11)     */
   Boolean    topology_change;                             /* (8.5.3.12)     */
   Time       topology_change_time;                        /* (8.5.3.13)     */
   Time       hold_time;                                   /* (8.5.3.14)     */
} Bridge_data;
/** Port Parameters (8.5.5) **/
typedef struct
{
   Port_id    port_id;                                     /* (8.5.5.1)      */
   State      state;                                       /* (8.5.5.2)      */
   Int        path_cost;                                   /* (8.5.5.3)      */
   Identifier designated_root;                             /* (8.5.5.4)      */
   Int        designated_cost;                             /* (8.5.5.5)      */
   Identifier designated_bridge;                           /* (8.5.5.6)      */
   Port_id    designated_port;                             /* (8.5.5.7)      */
   Boolean    topology_change_acknowledge;                 /* (8.5.5.8)      */
   Boolean    config_pending;                              /* (8.5.5.9)      */
   Boolean    change_detection_enabled;                    /* (8.5.5.10)      */
} Port_data;

typedef struct
{
   Boolean   active;         /* timer in use. */
   Time      value;          /* current value of timer, counting up. */
} Timer;



typedef struct {
  Bridge_data   bridge_info;                                 /* (8.5.3)        */
  Port_data     port_info[All_ports];                        /* (8.5.5)        */
  Config_bpdu   config_bpdu[All_ports];
                                                                      
  Tcn_bpdu      tcn_bpdu[All_ports];
  Timer         hello_timer;                                 /* (8.5.4.1)      */
  Timer         tcn_timer;                                   /* (8.5.4.2)      */
  Timer         topology_change_timer;                       /* (8.5.4.3)      */
  Timer         message_age_timer[All_ports];                /* (8.5.6.1)      */
  Timer         forward_delay_timer[All_ports];              /* (8.5.6.2)      */
  Timer         hold_timer[All_ports];                       /* (8.5.6.3)      */
  struct Device *device; /* A very cursed way to get access to device for sending BPDUs */
} STP_state;

#endif
