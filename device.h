#include <assert.h>
#include "raylib/raylib/include/raylib.h"
#include "8021d-stp.h"

#ifndef DEVICE_H
#define DEVICE_H 

#define MAX_PORTS 12



struct MAC {
  unsigned char value[6];
};

struct Frame {
  struct MAC destination_mac;
  struct MAC source_mac;
  short data_length; 
  char data[1500];
};

// same as Frame but with time_on_wire at the end 
// can be casted to Frame
struct FrameOnWire {
  struct Frame frame;
  float time_on_wire;
  bool arrived;
};


struct FrameOnDevice {
  struct Frame frame;
  int port_recieved;
};

const struct MAC BRIDGE_GROUP_ADDRESS = { .value = {0x01, 0x80, 0xc2, 0, 0, 0} };

struct Connection {
  int peer_port;
  struct MAC peer_mac;
  struct Device *peer_ptr;
  bool active; // if false, the other fields are meaningless
  // queue
  int queue_read_idx;
  int queue_write_idx;
  int queue_len;
  float travel_time;
  struct FrameOnWire queue[128];
};

enum DeviceType {
  DEVICE_SWITCH,
  DEVICE_ENDPOINT
};

struct Device {
  enum DeviceType type;
  bool is_evil; // only has meaning for endpoints
  Vector2 vis_position;
  float clock;
  struct MAC mac;
  struct MAC port_mac[MAX_PORTS];
  struct Connection port_connections[MAX_PORTS];
  STP_state stp_state;
  int port_count;
  // queue
  struct FrameOnDevice recv_queue[128];
  int recv_queue_read_idx;
  int recv_queue_write_idx;
  int recv_queue_len;
  struct FrameOnDevice send_queue[128];
  int send_queue_read_idx;
  int send_queue_write_idx;
  int send_queue_len;
};


void connection_enqueue(struct Connection *connection, struct Frame frame) {
  connection->queue_len++;
  connection->queue[connection->queue_write_idx++] = (struct FrameOnWire){.frame = frame, .time_on_wire = 0.0, .arrived = false};
  connection->queue_write_idx %= 128;
};

struct Frame connection_dequeue(struct Connection *connection) {
  connection->queue_len--;
  // printf("len: %i\n", connection->queue_len);
  assert(connection->queue_len >= 0);
  struct Frame frame = connection->queue[connection->queue_read_idx++].frame;
  connection->queue[connection->queue_read_idx-1] = (struct FrameOnWire){0}; // zero it out
  connection->queue_read_idx %= 128;
  return frame;
};

bool connection_frame_arrived(struct Connection *connection) {
  return connection->queue[connection->queue_read_idx].arrived;
};


#endif
