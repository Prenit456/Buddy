#pragma once
// Public TLS root used to verify the Buddy backend; no API keys live on the ESP32.
#include "HubCa.h"
#ifndef BUDDY_HUB_URL
#define BUDDY_HUB_URL "https://buddy-care-rat3.onrender.com"
#endif
#ifndef BUDDY_HUB_CA
#define BUDDY_HUB_CA ""
#endif

// TLS is fail-closed. Set the PEM root CA for your HTTPS Care Hub above.
// HTTP is accepted only for an explicitly configured private-LAN address.
