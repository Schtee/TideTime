#pragma once
#include <WebServer.h>
#include "Config.h"

extern WebServer server;

void setupRoutes();   // register ALL routes exactly once, called from setup()
void handleAdminSave();
void handleSetupSave();
