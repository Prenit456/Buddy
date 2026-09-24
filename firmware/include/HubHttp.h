#pragma once
#include <HTTPClient.h>
#include <IPAddress.h>
#include <WiFiClientSecure.h>
#include "HubConfig.h"
// A public Care Hub must be reached over TLS. Plain HTTP is only for a
// deliberately configured private-LAN prototype.
inline bool allowedHubUrl(const String& url, bool endpoint = false) {
  if (url.isEmpty()) return true;
  const bool tls = url.startsWith("https://");
  if (!tls && !url.startsWith("http://")) return false;
  const String remainder = url.substring(tls ? 8 : 7);
  const int slash = remainder.indexOf('/');
  if (!endpoint && slash >= 0) return false;
  const String authority = slash >= 0 ? remainder.substring(0, slash) : remainder;
  if (authority.isEmpty() || authority.indexOf('@') >= 0 ||
      authority.indexOf('?') >= 0 || authority.indexOf('#') >= 0) return false;
  if (tls) return true;
  const int colon = authority.lastIndexOf(':');
  const String host = colon >= 0 ? authority.substring(0, colon) : authority;
  if (host == "localhost") return true;
  IPAddress ip;
  if (!ip.fromString(host)) return false;
  return ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
         (ip[0] == 192 && ip[1] == 168) || ip[0] == 127;
}
inline bool beginHubHttp(HTTPClient& http,WiFiClient& plain,WiFiClientSecure& secure,const String& url,const String& token) {
  if (!allowedHubUrl(url, true)) return false;
  http.setConnectTimeout(3500);http.setTimeout(6000);
  bool ok=false;
  if(url.startsWith("https://")){if(strlen(BUDDY_HUB_CA)==0)return false;secure.setCACert(BUDDY_HUB_CA);ok=http.begin(secure,url);}
  else if(url.startsWith("http://"))ok=http.begin(plain,url);
  if(ok){http.addHeader("Content-Type","application/json");if(token.length())http.addHeader("Authorization","Bearer "+token);}
  return ok;
}
