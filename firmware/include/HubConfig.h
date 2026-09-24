#pragma once
// Optionally put private deployment configuration in HubSecrets.h (gitignored).
// BUDDY_HUB_URL may also be entered in the Wi-Fi setup portal.
#if __has_include("HubSecrets.h")
#include "HubSecrets.h"
#endif
#ifndef BUDDY_HUB_URL
#define BUDDY_HUB_URL ""
#endif
#ifndef BUDDY_HUB_CA
#define BUDDY_HUB_CA ""
#endif

// TLS is fail-closed. Set the PEM root CA for your HTTPS Care Hub above.
// HTTP is accepted only for an explicitly configured private-LAN address.
