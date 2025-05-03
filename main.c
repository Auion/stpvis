#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "8021d-stp.h"
#include "raylib/raylib/include/raylib.h"
#include "raylib/raylib/include/raymath.h"

#include "8021d-stp.c"
#include "device.h"


#define PING_COLOR (Color){0x7d, 0xaa, 0xc8, 0xff};
#define BPDU_CONFIG_COLOR (Color){0xb1, 0x7c, 0xa3, 0xff};
#define BPDU_TCN_COLOR (Color){0xed, 0xb5, 0x95, 0xff};

#define DEFAULT_TRAVEL_TIME 1.0

bool mac_equal(struct MAC mac1, struct MAC mac2) {
  for (unsigned int i = 0; i < sizeof(mac1); i++) {
    if (mac1.value[i] != mac2.value[i]) return false;
  }
  return true;
}

bool frame_is_bpdu_config(struct Frame frame) {
  if (frame.data_length == (sizeof (Config_bpdu))
    && mac_equal(frame.destination_mac, BRIDGE_GROUP_ADDRESS)) {
    return true;
  }
  return false;
}

bool frame_is_bpdu_tcn(struct Frame frame) {
  if (frame.data_length == (sizeof (Tcn_bpdu))
    && mac_equal(frame.destination_mac, BRIDGE_GROUP_ADDRESS)) {
    return true;
  }
  return false;
}


struct MAC random_mac() {
  struct MAC mac = {0};
  for (unsigned int i = 0; i < (sizeof mac.value); i++) {
    mac.value[i] = rand() % 255;
  };
  return mac;
}

// write the mac directly into a long
long mac_into_long(struct MAC mac) {
  long val = 0;
  
  val += mac.value[0];
  val <<= 8;
  val += mac.value[1];
  val <<= 8;
  val += mac.value[2];
  val <<= 8;
  val += mac.value[3];
  val <<= 8;
  val += mac.value[4];
  val <<= 8;
  val += mac.value[5];
  val <<= 8;

  return val;
}

// given a mac address and a prority value, returns the identifier, host endian
Identifier bridge_identifier_from_mac_and_priority(struct MAC mac, unsigned short priority) {
  Identifier id = 0;
  
  id = (long)priority << (8*6);
  id += mac_into_long(mac);

  return id;
}


enum Action {
  ACTION_NONE,
  ACTION_CONNECTING_SWITCHES
};


// Note that STP assumes Ethernet



void connection_tick(struct Connection *connection, float delta) {
  // queue length is not normally clamped, since it is useful to know
  // if the queue has been overused. It must be clamped here.
  int clamped_queue_len = connection->queue_len <= 256 ? connection->queue_len : 256;
  for (int offset = 0; offset < clamped_queue_len; offset++) {
    struct FrameOnWire *frame = &connection->queue[(connection->queue_read_idx + offset) % 128]; 
    frame->time_on_wire += delta;
    if (frame->time_on_wire > connection->travel_time) {
      frame->arrived = true;
    }
  };
};


void device_port_recieve_frame(struct Device *device, int recv_port, struct Frame frame) {
  device->recv_queue_len++;
  device->recv_queue[device->recv_queue_write_idx++] = (struct FrameOnDevice){frame, recv_port};
  device->recv_queue_write_idx %= 128;
};

// Takes a frame from the recv queue, does stuff, then puts a frame
// in the send queue (maybe).
// (Currently just copies frame recv to send queue)
void device_process_frame(struct Device *device) {
  if (device->recv_queue_len <= 0) return;
  device->recv_queue_len--;
  struct FrameOnDevice dev_frame = device->recv_queue[device->recv_queue_read_idx++];
  device->recv_queue_read_idx %= 128;
  // check for Config bpdu
  if (dev_frame.frame.data_length == (sizeof(Config_bpdu))) {
    Config_bpdu bpdu = {0};
    memcpy(&bpdu, dev_frame.frame.data, dev_frame.frame.data_length);
    received_config_bpdu(&device->stp_state, (dev_frame.port_recieved+1), &bpdu);

  } else if (dev_frame.frame.data_length == (sizeof(Tcn_bpdu))){
    Tcn_bpdu bpdu = {0};
    memcpy(&bpdu, dev_frame.frame.data, dev_frame.frame.data_length);
    received_tcn_bpdu(&device->stp_state, (dev_frame.port_recieved+1), bpdu);

  } else {
    // simple repeat
    if (device->stp_state.port_info[dev_frame.port_recieved+1].state == Forwarding) {
      device->send_queue_len++;
      device->send_queue[device->send_queue_write_idx++] = dev_frame;
      device->send_queue_write_idx %= 128;
    };
  };
  
};

// sends frame in send queue to all relevant ports
void device_send_frame(struct Device *device) {
  // TODO: Consult MAC table to ensure frame isn't sent back to where it came from
  // For now, send_queue will stick with FrameOnDevice instead of Frame.
  if (device->send_queue_len <= 0) return;
  device->send_queue_len--;
  struct FrameOnDevice dev_frame = device->send_queue[device->send_queue_read_idx++];
  device->send_queue_read_idx %= 128;
  for (int i = 0; i < device->port_count; i++) {
    if (device->port_connections[i].active == false) continue; 
    if (dev_frame.port_recieved == i) continue;
    if (device->stp_state.port_info[i+1].state == Forwarding) {
      connection_enqueue(&device->port_connections[i], dev_frame.frame);
    };
  };
};

int main() {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  SetConfigFlags(FLAG_VSYNC_HINT);
  InitWindow(800, 600, "stpvis");
  SetExitKey(0);
  
  struct Device *device_list = calloc(256, (sizeof *device_list));
  int device_list_len = 0;


  Camera2D cam = (Camera2D){
    .zoom = 1.8f,
    .offset = {0},
    .target = {0},
    .rotation = 0.0f
  };
  struct Device *selected_device = NULL;
  int selected_port = 0;
  enum Action action = ACTION_NONE;
  enum DeviceType device = DEVICE_SWITCH;
  bool sim_paused = false;
  while (!WindowShouldClose()) {
    cam.offset = (Vector2){GetScreenWidth()/2.0, GetScreenHeight()/2.0};
    float sim_delta = GetFrameTime();
    if (sim_paused == true) {
      sim_delta = 0.0;
    }
    cam.zoom += GetMouseWheelMove()*0.20;
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
      cam.target = Vector2Add(cam.target, Vector2Negate(GetMouseDelta()));
    };

    if (IsKeyPressed(KEY_SPACE)) sim_paused = !sim_paused;
    if (IsKeyPressed(KEY_ONE)) device = DEVICE_SWITCH;
    if (IsKeyPressed(KEY_TWO)) device = DEVICE_ENDPOINT;
    if (IsKeyPressed(KEY_ESCAPE)) {
      selected_device = NULL;
      action = ACTION_NONE;
    };
    struct Device *device_hovered = NULL;
    for (int i = 0; i < device_list_len; i++) {
      struct Device *i_device = &device_list[i];
      Rectangle switch_rect = (Rectangle){
        .x = i_device->vis_position.x,
        .y = i_device->vis_position.y,
        .width = 20*(4),
        .height = 20,
      };
      if (CheckCollisionPointRec(GetScreenToWorld2D(GetMousePosition(), cam), switch_rect)) {
        device_hovered = i_device;
      };
    };
    // removal actions
    if (device_hovered && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
      for (int port = 0; port < (4); port++) {
        Rectangle port_rect = (Rectangle){
          .x = device_hovered->vis_position.x + 20*port,
          .y = device_hovered->vis_position.y,
          .width = 20,
          .height = 20,
        };
        if (CheckCollisionPointRec(GetScreenToWorld2D(GetMousePosition(), cam), port_rect)) {
          // remove connections here.
          int self_port = port;
          if (device_hovered == NULL) continue;
          struct Device *peer_device = device_hovered->port_connections[self_port].peer_ptr;
          if (peer_device == NULL) continue;
          int peer_port = device_hovered->port_connections[port].peer_port;
          device_hovered->port_connections[port] = (struct Connection){0}; // zero it out
          peer_device->port_connections[peer_port] = (struct Connection){0};
        };
      };
    };

    static struct Device *dragging_switch = NULL;
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
      if (device_hovered && !dragging_switch) {
        dragging_switch = device_hovered;
      };
      if (dragging_switch) {
        dragging_switch->vis_position = GetScreenToWorld2D(GetMousePosition(), cam);
      };
    } else {
      dragging_switch = NULL;
    };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      // click checking for switches
      struct Device *device_clicked = device_hovered;
      int port_clicked = 0;
      bool is_port_clicked = false;
      if (device_clicked) {
        for (int port = 0; port < device_clicked->port_count; port++) {
          Rectangle port_rect = (Rectangle){
            .x = device_clicked->vis_position.x + 20*port,
            .y = device_clicked->vis_position.y,
            .width = 20,
            .height = 20,
          };
          if (CheckCollisionPointRec(GetScreenToWorld2D(GetMousePosition(), cam), port_rect)) {
            is_port_clicked = true;
            port_clicked = port;
            printf("port clicked: %i\n", port_clicked);
          } else {
            device_clicked->is_evil = !device_clicked->is_evil;
          };
        };
      }
      if (action == ACTION_NONE && device_clicked && is_port_clicked) {
        if (port_clicked >= device_clicked->port_count || selected_port >= selected_device->port_count) {
          // device_clicked = NULL;
          // continue;
        };
        action = ACTION_CONNECTING_SWITCHES;
        selected_device = device_clicked;
        selected_port = port_clicked;
      } else if (action == ACTION_CONNECTING_SWITCHES && device_clicked) {
        // Connnections are uni-direction. Two must be made.
        if (selected_device->port_connections[selected_port].active == true 
          ||device_clicked->port_connections[port_clicked].active == true) {
          selected_device = NULL;
          action = ACTION_NONE;
          continue;
        };
        selected_device->port_connections[selected_port] = (struct Connection){
          // .peer_mac = device_clicked->mac,
          .peer_port = port_clicked,
          .peer_ptr = device_clicked,
          .active = true,
          .queue_read_idx = 0,
          .queue_write_idx = 0,
          .queue_len = 0,
          .travel_time = DEFAULT_TRAVEL_TIME,
        };
        device_clicked->port_connections[port_clicked] = (struct Connection){
          // .peer_mac = selected_device->mac,
          .peer_port = selected_port,
          .peer_ptr = selected_device,
          .active = true,
          .queue_read_idx = 0,
          .queue_write_idx = 0,
          .queue_len = 0,
          .travel_time = DEFAULT_TRAVEL_TIME,
        };
        printf("Bi-directional connection created\n");
        
        selected_device = NULL;
        action = ACTION_NONE;
      };
      if (!device_clicked) {
        // ADD A DEVICE
        int port_count = 4;
        enum DeviceType type = DEVICE_SWITCH;
        if (device == DEVICE_ENDPOINT) {
          port_count = 1;
          type = DEVICE_ENDPOINT;
        };
        device_list[device_list_len] = (struct Device){
          .vis_position = GetScreenToWorld2D(GetMousePosition(), cam),
          .clock = 0.0,
          .port_connections = {{0}},
          .port_count = port_count,
          .type = type,
          .stp_state = {{0}}
        };
        STP_state *stp_state = &device_list[device_list_len].stp_state;
        // cursed but works
        stp_state->device = &device_list[device_list_len];

        for (int i = One; i < All_ports; i++) {
          // device_list[device_list_len].port_mac[i] = (struct MAC){ .value = {device_list_len*50+i}};
          device_list[device_list_len].port_mac[i-1] = random_mac();
          Port_id port_id = i; // is this correct? no mac used?
          set_port_priority(stp_state, i, port_id);
          stp_state->port_info[i].path_cost = 1; // cannot be zero!
          if (i > port_count) {
            set_port_state(stp_state, i, Disabled);                 /* (8.7.5 b1)    */
          };
        };
        // for now, device mac will be taken from port 1's mac (the stp spec allows this)
        struct MAC device_mac = device_list[device_list_len].port_mac[0];
        device_list[device_list_len].mac = device_mac;
        Identifier bridge_id = bridge_identifier_from_mac_and_priority(device_mac, 32767);
        if (type == DEVICE_SWITCH) {
          // bridge id must be set before init.
          set_bridge_priority(stp_state, bridge_id);
          // these are set to their recommended values (802.1d 1994 table 8-3)
          // since they're encoded as 1/256 of a second, they must be *256.
          stp_state->bridge_info.bridge_max_age = 20 * 256; 
          stp_state->bridge_info.bridge_hello_time = 2 * 256;
          stp_state->bridge_info.bridge_forward_delay = 15 * 256;
          stp_initialisation(stp_state);
        };
        device_list_len++;
        printf("Added device\n");
      };
    };

    // BEGIN DEVICE LOGIC 
    for (int i = 0; i < device_list_len; i++) {
      struct Device *i_device = &device_list[i];
      if (i_device->type == DEVICE_ENDPOINT) {
        if (i_device->port_connections[0].active == false) continue;
        if (i_device->clock < 1.0) continue;
        i_device->clock = 0.0;
        struct Frame new_frame = (struct Frame){0};
        connection_enqueue(&i_device->port_connections[0], new_frame);
      };
    };

    for (int i = 0; i < device_list_len; i++) {
      struct Device *device = &device_list[i];
      device->clock += sim_delta;
      for (int port = 0; port < device->port_count; port++) {
        // device->stp_state.port_info[port+1].state = Forwarding; // NO STP
        if (device->port_connections[port].active == false) continue;
        struct Connection *con = &device->port_connections[port];
        // TICK ALL CONNECTION FRAMES 
        connection_tick(con, sim_delta);
        // SWITCHES COLLECT "ARRIVED" FRAMES FROM CONNECTIONS
        struct Device *peer_device = con->peer_ptr;
        if (connection_frame_arrived(con)) {
          struct Frame frame = connection_dequeue(con);
          // behavior is device-specific
          device_port_recieve_frame(peer_device, con->peer_port, frame);
        };
      };

      // SWITCHES SEND OUT ONE QUEUED FRAME (IF ANY).
      // ENDPOINTS ACT AS SINKS AND EAT (IGNORE) THEIR FRAMES.
      if (device->type == DEVICE_SWITCH) {
        device_process_frame(device);
        device_send_frame(device);
      };

      // TICK STP STATE (256 times per second)
      for (int i = 0; i < (sim_delta * 256); i++) {
        tick(&device->stp_state);
      };

      if (device->is_evil == true && device->type == DEVICE_ENDPOINT) {
        unsigned long evil_id = 0x0001000000000000;
        Config_bpdu evil_bpdu = (Config_bpdu) {
          .port_id = 1,
          .root_id = evil_id,
          .root_path_cost = 0,
          .bridge_id = evil_id,
          .topology_change = 1,
          .type = Config_bpdu_type,
          .hello_time = 2*256,
          .max_age = 20*256,
          .forward_delay = 15*256,
        };
        struct Frame evil_frame = {0};
        evil_frame.data_length = (sizeof(evil_bpdu));
        evil_frame.destination_mac = BRIDGE_GROUP_ADDRESS;
        memcpy(evil_frame.data, &evil_bpdu, evil_frame.data_length);
        // spam a topo change bpdu.
        connection_enqueue(&device->port_connections[0], evil_frame);

        // we can be lazy for reasons 
        // struct Frame evil_frame2 = {0};
        // evil_frame2.data_length = (sizeof(Tcn_bpdu));
        // evil_frame2.destination_mac = BRIDGE_GROUP_ADDRESS;
        // connection_enqueue(&device->port_connections[0], evil_frame2);
      };
    };
    


    
    assert(device_list_len < 256);
    BeginDrawing();
    ClearBackground(RAYWHITE);
    BeginMode2D(cam);

    // Draw devices
    for (int i = 0; i < device_list_len; i++) {
      struct Device device = device_list[i];
      Vector2 device_pos = device.vis_position;
      if (device.type == DEVICE_SWITCH) {
        DrawRectangle(device.vis_position.x, device.vis_position.y, 20*device.port_count, 20, BLACK);
        for (int port = 0; port < device.port_count; port++) {
          Color port_color = device.port_connections[port].active ? GREEN : RED;
          if (device.stp_state.bridge_info.root_port-1 == port) {
            port_color = PURPLE;
          };
          DrawCircle(device_pos.x + (20*port)+10, device_pos.y + 10.0, 8.0, port_color);
          char port_state[64] = {0};
          sprintf(port_state, "%i", device.stp_state.port_info[port+1].port_id);
          DrawText(port_state, device_pos.x + (20*port), device_pos.y + 20, 20, BLACK);
          Vector2 text_pos = (Vector2){device_pos.x+(20*port), device_pos.y+40};
          const Vector2 ZERO = (Vector2){0,0};
          if (device.stp_state.port_info[port+1].state == Listening) {
            DrawTextPro(GetFontDefault(), "listening", text_pos, ZERO, 35, 15, 1, BLACK);
            // DrawText("listening", device_pos.x+(20*port), device_pos.y+40, 1, BLACK);
          } else if (device.stp_state.port_info[port+1].state == Learning) {
            DrawTextPro(GetFontDefault(), "learning", text_pos, ZERO, 35, 15, 1, BLACK);
            // DrawText("learning", device_pos.x+(20*port), device_pos.y+40, 1, BLACK);
          } else if (device.stp_state.port_info[port+1].state == Forwarding) {
            DrawTextPro(GetFontDefault(), "forwarding", text_pos, ZERO, 35, 15, 1, BLACK);
            // DrawText("forwarding", device_pos.x+(20*port), device_pos.y+40, 1, BLACK);
          } else if (device.stp_state.port_info[port+1].state == Blocking) {
            DrawTextPro(GetFontDefault(), "blocking", text_pos, ZERO, 35, 15, 1, BLACK);
            // DrawText("blocking", device_pos.x+(20*port), device_pos.y+40, 1, BLACK);
          };

        };
      } else if (device.type == DEVICE_ENDPOINT) {
        if (device.is_evil) {
          DrawText("evil mode", device.vis_position.x, device.vis_position.y-20, 20, RED);
        }
        DrawRectangle(device.vis_position.x, device.vis_position.y, 20*device.port_count+20, 20, BLACK);
        for (int port = 0; port < device.port_count; port++) {
          Color port_color = device.port_connections[port].active ? GREEN : RED;
          DrawCircle(device_pos.x + (20*port)+10, device_pos.y + 10.0, 8.0, port_color);
        };
      };

    };
    // Draw new connection 
    if (action == ACTION_CONNECTING_SWITCHES && selected_device != NULL) {
      Vector2 self_position = (Vector2){selected_device->vis_position.x+10 + 20*selected_port, selected_device->vis_position.y+10};
      DrawLineEx(self_position, GetScreenToWorld2D(GetMousePosition(), cam), 5.0, GRAY);
    };
    // Draw existing connections 
    for (int i = 0; i < device_list_len; i++) {
      struct Device i_switch = device_list[i];
      for (int self_port = 0; self_port < i_switch.port_count; self_port++) {
        if (i_switch.port_connections[self_port].active == false) continue;
        
        struct Device *peer_device = i_switch.port_connections[self_port].peer_ptr;
        if (peer_device == NULL) continue;
        int peer_port = i_switch.port_connections[self_port].peer_port;
        Vector2 self_position = (Vector2){i_switch.vis_position.x+10 + 20*self_port, i_switch.vis_position.y+10};
        Vector2 peer_position = (Vector2){peer_device->vis_position.x+10 + 20*peer_port, peer_device->vis_position.y+10};
        DrawLineEx(self_position, peer_position, 5.0, BLACK);
        // Warning for overloaded wires
        if (i_switch.port_connections[self_port].queue_len >= 128 
          ||peer_device->port_connections[peer_port].queue_len >= 128) {
          Vector2 center = Vector2Lerp(self_position, peer_position, 0.5);
          DrawTextEx(GetFontDefault(), "!!", center, 20, 1.0, RED);
        }; 
      };
    };
    
    // draw frames (layer 2) on the wire 
    for (int i = 0; i < device_list_len; i++) {
      struct Device *src_device = &device_list[i];
      for (int port = 0; port < src_device->port_count; port++) {
        struct Connection con = src_device->port_connections[port];
        if (con.active == false) continue;
        struct Device *dst_device = con.peer_ptr;

        for (int offset = 0; offset < con.queue_len; offset++) {
          struct FrameOnWire frame_on_wire = con.queue[((con.queue_read_idx + offset) % 128)];
          Vector2 wire_start = (Vector2){ src_device->vis_position.x+10 + 20*port, src_device->vis_position.y+10 };
          Vector2 wire_end = (Vector2){ dst_device->vis_position.x+10 + 20*con.peer_port, dst_device->vis_position.y+10 };
          float time_norm = Normalize(frame_on_wire.time_on_wire, 0.0, con.travel_time);
          Vector2 on_wire = Vector2Lerp(wire_start, wire_end, time_norm);
          Color color = PING_COLOR;
          if (frame_is_bpdu_config(*(struct Frame*)&frame_on_wire)) {
            color = BPDU_CONFIG_COLOR;
          } else if (frame_is_bpdu_tcn(*(struct Frame*)&frame_on_wire)) {
            color = BPDU_TCN_COLOR;
          }
          DrawCircleV(on_wire, 7.0, color);
        };
      };
    };

    for (int i = 0; i < device_list_len; i++) {
      struct Device *device = &device_list[i];
      if (device->type != DEVICE_SWITCH) continue;
      char mac_text[128] = {0};
      sprintf(mac_text, "%x:%x:%x:%x:%x:%x", device->mac.value[0], device->mac.value[1], device->mac.value[2], device->mac.value[3], device->mac.value[4], device->mac.value[5]);
      DrawText(mac_text, device->vis_position.x, device->vis_position.y-15, 15, BLACK);
      if (device->recv_queue_len >= 128) {
        DrawText("!! overloaded", device->vis_position.x, device->vis_position.y-55, 20, RED);
      };
      if (device->stp_state.bridge_info.bridge_id == device->stp_state.bridge_info.designated_root) {
        DrawText("I am root!", device->vis_position.x, device->vis_position.y-35, 20, BLACK);
      }
    };


    EndMode2D();

    // status
    char *device_text = "Placing: Switch";
    if (device == DEVICE_ENDPOINT) {
      device_text = "Placing: Endpoint";
    };
    DrawText(device_text, 10, 10, 20, BLACK);
    EndDrawing();
  }; //END while
  

  free(device_list);
}
