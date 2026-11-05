#ifndef NEON_BADGES_H
#define NEON_BADGES_H

#include <stdint.h>
#include "../../shared/matrix_protocol.h"

namespace neon {
using matrix_indicator::State;
constexpr int WIDTH = 16;
constexpr int HEIGHT = 8;
constexpr uint32_t FRAME_MS = 50;
constexpr uint32_t TRANSITION_MS = 150;
constexpr uint32_t STARTUP_MS = 1600;
struct Color { uint8_t r, g, b; };
static_assert(sizeof(Color) == 3, "RGB frame layout changed");
constexpr Color GREEN{0,255,0}, BLUE{0,80,255}, AMBER{255,162,0}, WHITE{235,245,255};
struct Frame {
  Color pixels[WIDTH * HEIGHT]{};
  void clear() { for (auto& p : pixels) p = {0,0,0}; }
  Color& at(int x, int y) { return pixels[y * WIDTH + x]; }
};
inline Color scale(Color c, unsigned numerator, unsigned denominator = 1000) {
  return {static_cast<uint8_t>((c.r * numerator + denominator/2) / denominator),
          static_cast<uint8_t>((c.g * numerator + denominator/2) / denominator),
          static_cast<uint8_t>((c.b * numerator + denominator/2) / denominator)};
}
inline Color mix(Color a, Color b, unsigned amount, unsigned total) {
  return {static_cast<uint8_t>((a.r*(total-amount)+b.r*amount)/total),
          static_cast<uint8_t>((a.g*(total-amount)+b.g*amount)/total),
          static_cast<uint8_t>((a.b*(total-amount)+b.b*amount)/total)};
}
inline int absolute(int n) { return n < 0 ? -n : n; }

inline void glyph(Frame& f, char ch, int x, Color c) {
  static constexpr uint8_t E[] = {15,8,14,8,8,15};
  static constexpr uint8_t V[] = {9,9,9,10,10,4};
  static constexpr uint8_t G[] = {7,8,11,9,9,7};
  static constexpr uint8_t A[] = {6,9,9,15,9,9};
  static constexpr uint8_t S[] = {7,8,6,1,1,14};
  static constexpr uint8_t M[] = {17,27,21,17,17,17};
  const uint8_t* rows = E;
  switch (ch) {
    case 'V': rows=V; break; case 'G': rows=G; break;
    case 'A': rows=A; break; case 'S': rows=S; break;
    case 'M': rows=M; break; default: break;
  }
  const int width = ch == 'M' ? 5 : 4;
  for (int y=0; y<6; ++y)
    for (int dx=0; dx<width; ++dx)
      if (rows[y] & (1 << (width-1-dx))) f.at(x+dx,y+2)=c;
}

inline void drawBody(Frame& f, State s, uint32_t phase) {
  f.clear();
  if (s.flags & matrix_indicator::ENGINE) {
    glyph(f,'G',0,AMBER); glyph(f,'A',5,AMBER); glyph(f,'S',10,AMBER);
    f.at(15,4+(phase/4500)%2)=scale(AMBER,500);
    return;
  }
  const bool reverse = matrix_indicator::regen(s);
  const Color c = reverse ? BLUE : GREEN;
  glyph(f,'E',0,c); glyph(f,'V',5,c);
  if (s.flags & matrix_indicator::EV_MODE) {
    glyph(f,'M',11,BLUE);
  } else if (matrix_indicator::active(s)) {
    static constexpr uint8_t ARROW[] = {4,2,31,2,4};
    const int sweep = static_cast<int>(phase)-2000;
    const int head = reverse ? 15000-sweep : 11000+sweep;
    for (int y=0; y<5; ++y) for (int dx=0; dx<5; ++dx) {
      const int bit = reverse ? dx : 4-dx;
      if (!(ARROW[y] & (1 << bit))) continue;
      const int distance = absolute((11+dx)*1000-head);
      const unsigned strength = distance < 1600 ? 1600-distance : 0;
      f.at(11+dx,3+y)=mix(c,WHITE,strength,1600);
    }
  } else {
    for (int x=12; x<16; ++x) f.at(x,5)=scale(c,450);
  }
}

inline void drawMeter(Frame& f, State s) {
  const bool reverse = matrix_indicator::regen(s);
  const bool power = (s.flags & matrix_indicator::POWER) && !reverse;
  const bool gas = (s.flags & matrix_indicator::ENGINE) != 0;
  const int count = absolute(s.step) > 16 ? 16 : absolute(s.step);
  for (int i=0; i<WIDTH; ++i) {
    const int x = reverse ? 15-i : i;
    // Deliberately separate meter hues from green EV / amber GAS / blue M.
    const Color c = reverse ? Color{0,255,255} : power ? Color{255,0,0} :
        gas ? (x<6 ? Color{255,64,0} : x<12 ? Color{255,32,0} : Color{255,0,0}) :
        x<6 ? Color{0,255,255} : x<10 ? Color{120,0,255} :
        x<14 ? Color{220,0,255} : Color{255,0,120};
    if (i<count) f.at(x,0)=c;

  }
}

inline void startup(Frame& f, uint32_t elapsed) {
  f.clear();
  static constexpr Color RAINBOW[] = {{255,0,0},{255,127,0},{255,255,0},
      {0,255,0},{0,0,255},{75,0,130},{148,0,211}};
  static constexpr Color STRIPES[] = {{0,255,255},{255,105,180},{255,255,255},
      {255,105,180},{0,255,255}};
  const unsigned columns = elapsed >= 750 ? 16 : elapsed/50+1;
  for (unsigned x=0; x<columns; ++x) for (int y=0; y<7; ++y) f.at(x,y)=RAINBOW[y];
  if (elapsed >= 800)
    for (int x=0; x<5; ++x) for (int y=0; y<HEIGHT; ++y) f.at(x,y)=STRIPES[x];
}

inline uint8_t identity(State s) {
  if (s.flags & matrix_indicator::ENGINE) return 1;
  return 2 | ((s.flags & matrix_indicator::EV_MODE) ? 4 : 0) |
      (matrix_indicator::regen(s) ? 8 : 0);
}

class Renderer {
 public:
  void update(State next, uint32_t now) {
    if (initialized_) advance(now);
    lastTick_=now;
    if (initialized_ && identity(next) != identity(state_)) {
      // Snapshot the currently visible body, including an interrupted dissolve.
      Frame current;
      body(current,now);
      from_ = current;
      transitionStart_ = now;
      transitioning_ = true;
    }
    state_ = next;
    initialized_ = true;
  }
  void render(Frame& f, uint32_t now) {
    if (transitioning_ && static_cast<uint32_t>(now-transitionStart_) >= TRANSITION_MS)
      transitioning_ = false;
    advance(now);
    body(f,now);
    drawMeter(f,state_); // The top bar remains immediate and unchanged.
    echo(f,now);
    const uint8_t brightness = matrix_indicator::brightness(state_);
    if (brightness != 255) for (auto& p : f.pixels) p=scale(p,brightness,255);
  }
 private:
  void advance(uint32_t now) {
    uint32_t remaining=now-lastTick_;
    lastTick_=now;
    // An old/off frame must not cause an unbounded catch-up loop.
    if (remaining>1000) remaining=1000;
    while (remaining) {
      const unsigned dt=remaining>20 ? 20 : remaining;
      remaining-=dt;
      const unsigned rate=(state_.flags & matrix_indicator::POWER) ? 36 :
          2+absolute(state_.step);
      phase_=(phase_+dt*rate)%9000;

    }
  }
  static bool equal(Color a, Color b) {
    return a.r==b.r && a.g==b.g && a.b==b.b;
  }
  void echo(Frame& f, uint32_t now) {
    // Replay every visible top-row change two seconds later. A falling pixel
    // echoes its departing color, rather than trying to emit a black pulse.
    while (eventCount_ && static_cast<uint32_t>(now-events_[eventHead_].at)>=2000) {
      const EchoEvent& event=events_[eventHead_];
      for (int x=0; x<WIDTH; ++x) if (!equal(event.row[x],delayed_[x])) {
        const Color next=event.row[x];
        echoColor_[x]=(next.r || next.g || next.b) ? next : delayed_[x];
        echoAt_[x]=event.at+2000;
        echoActive_[x]=true;
        delayed_[x]=next;
      }
      eventHead_=(eventHead_+1)%ECHO_CAPACITY;
      --eventCount_;
    }
    bool changed=false;
    for (int x=0; x<WIDTH; ++x) if (!equal(f.at(x,0),recorded_[x])) changed=true;
    if (changed) {
      if (eventCount_==ECHO_CAPACITY) {
        eventHead_=(eventHead_+1)%ECHO_CAPACITY;
        --eventCount_;
      }
      EchoEvent& event=events_[(eventHead_+eventCount_)%ECHO_CAPACITY];
      event.at=now;
      for (int x=0; x<WIDTH; ++x) event.row[x]=recorded_[x]=f.at(x,0);
      ++eventCount_;
    }
    for (int x=0; x<WIDTH; ++x) if (echoActive_[x]) {
      const uint32_t age=now-echoAt_[x];
      if (age>=2000) { echoActive_[x]=false; continue; }
      const unsigned strength=age<400 ? age*350/400 : (2000-age)*350/1600;
      f.at(x,1)=scale(echoColor_[x],strength);
    }
  }
  void body(Frame& f, uint32_t now) const {
    drawBody(f,state_,phase_);
    const uint32_t elapsed = now-transitionStart_;
    if (transitioning_ && elapsed < TRANSITION_MS) {
      for (int i=2*WIDTH; i<WIDTH*HEIGHT; ++i)
        f.pixels[i]=mix(from_.pixels[i],f.pixels[i],elapsed,TRANSITION_MS);
    }
  }
  // Save actual RGB changes, so old EV/GAS/regen colors survive transitions.
  struct EchoEvent { uint32_t at; Color row[WIDTH]; };
  static constexpr unsigned ECHO_CAPACITY=64;
  EchoEvent events_[ECHO_CAPACITY]{};
  unsigned eventHead_=0, eventCount_=0;
  Color recorded_[WIDTH]{}, delayed_[WIDTH]{}, echoColor_[WIDTH]{};
  uint32_t echoAt_[WIDTH]{};
  bool echoActive_[WIDTH]{};
  uint32_t lastTick_=0;
  uint32_t phase_=0;
  State state_{};
  bool initialized_ = false;
  bool transitioning_ = false;
  uint32_t transitionStart_ = 0;
  Frame from_{};
};
}  // namespace neon
#endif
