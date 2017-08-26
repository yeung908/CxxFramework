#if defined(__linux__)

#include <string.h>
#include <sys/errno.h>
#include <sys/epoll.h>
#include <map>

#include <OSMutex.h>
#include "epollev.h"

using namespace std;

#define MAX_EPOLL_FD    20000

static int epollfd = 0;              // epoll 描述符
static epoll_event *_events = NULL;  // epoll事件接收数组
static int m_curEventReadPos = 0;    // 当前读事件位置，在epoll事件数组中的位置
static int m_curTotalEvents = 0;     // 总的事件个数，每次epoll_wait之后更新
static OSMutex sMaxFDPosMutex;       // 锁
static bool canEpoll = false;        // 是否可以执行epoll_wait,避免频繁执行，浪费CPU
static map<int, void *> epollFdmap;  // 映射 fd和对应的RTSPSession对象


void select_startevents() {
  epollfd = epoll_create(MAX_EPOLL_FD);

  if (_events == NULL) {
    _events = new epoll_event[MAX_EPOLL_FD];  // we only listen the read event
  }
  if (_events == NULL) {
    perror("new epoll_event error:");
    exit(1);
  }
  m_curEventReadPos = 0;
  m_curTotalEvents = 0;
}

int epoll_stopevents() {
  delete[] _events;
  return 0;
}

int select_modwatch(struct eventreq *req, int which) {
  if (req == NULL) {
    return -1;
  }
  struct epoll_event ev;
  memset(&ev, 0x0, sizeof(ev));

  int ret = -1;
  OSMutexLocker locker(&sMaxFDPosMutex);  // 加锁，防止线程池中的多个线程执行该函数，导致插入监听事件失败
  if (which == EV_RE) {
    ev.data.fd = req->er_handle;
    ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;  // level trigger

    ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, req->er_handle, &ev);
  } else if (which == EV_WR) {
    ev.data.fd = req->er_handle;
    ev.events = EPOLLOUT;  // level trigger

    ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, req->er_handle, &ev);
  } else if (which == EV_RM) {
    ret = epoll_ctl(epollfd,
                    EPOLL_CTL_DEL,
                    req->er_handle,
                    NULL);  // remove all this fd events
  } else {  // epoll can not listen RESET
    // we dont needed
  }

  epollFdmap[req->er_handle] = req->er_data;
  canEpoll = true;  // 每次新增事件后可以马上执行epoll_wait，新增了监听事件，意味着接下来可能马上要发生事件在这个fd上
  return 0;
}

int select_watchevent(struct eventreq *req, int which) {
  return select_modwatch(req, which);
}

int select_removeevent(int which) {
  int ret = -1;
  OSMutexLocker locker(&sMaxFDPosMutex);
  ret = epoll_ctl(epollfd, EPOLL_CTL_DEL, which, NULL);  // remove all this fd events
  canEpoll = true;  // 每删除一个fd后，可以马上执行epoll_wait，可能在删除掉的fd上出现读异常
  return 0;
}

int epoll_waitevent() {
  if (canEpoll == false) {
    return -1;
  }

  int nfds = 0;
  int curreadPos = -1;  // m_curEventReadPos;//start from 0
  if (m_curTotalEvents <= 0) {  //当前一个epoll事件都没有的时候，执行epoll_wait
    m_curTotalEvents = 0;
    m_curEventReadPos = 0;
    nfds = epoll_wait(epollfd, _events, MAX_EPOLL_FD, 15000);

    if (nfds > 0) {
      canEpoll = false;  // wait到了epoll事件，先处理完再wait
      m_curTotalEvents = nfds;
    } else if (nfds < 0) {

    } else if (nfds == 0) {
      canEpoll = true;  // 这一次wait超时了，下一次继续wait
    }
  }

  if (m_curTotalEvents) {  // 从事件数组中每次取一个，取的位置通过m_curEventReadPos设置
    curreadPos = m_curEventReadPos;
    m_curEventReadPos++;
    if (m_curEventReadPos >= m_curTotalEvents - 1) {
      m_curEventReadPos = 0;
      m_curTotalEvents = 0;
    }
  }

  return curreadPos;
}

int select_waitevent(struct eventreq *req, void *onlyForMOSX) {
  int eventPos = -1;
  eventPos = epoll_waitevent();
  if (eventPos >= 0) {
    req->er_handle = _events[eventPos].data.fd;
    if (_events[eventPos].events == EPOLLIN ||
        _events[eventPos].events == EPOLLHUP ||
        _events[eventPos].events == EPOLLERR) {
      req->er_eventbits = EV_RE;  // we only support read event
    } else if (_events[eventPos].events == EPOLLOUT) {
      req->er_eventbits = EV_WR;
    }
    req->er_data = epollFdmap[req->er_handle];
    OSMutexLocker locker(&sMaxFDPosMutex);
    select_removeevent(req->er_handle);
    return 0;
  }
  return EINTR;
}

#endif
