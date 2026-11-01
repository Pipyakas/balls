#include "control/ControlServer.h"

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(int) { return false; }

void ControlServer::stop() { running = false; }

void ControlServer::pump(const Handler&) {}

void ControlServer::completeDeferred(const Completion&) {}
