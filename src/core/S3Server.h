//
// S3Server - minimal S3-compatible API over LittleFS (path-style; buckets
// app = user files (read/write), web = /www, examples = /scripts/examples,
// fonts = /fonts (read-only)), for the web IDE (@aws-sdk/client-s3) and S3
// CLI tools (aws --no-sign-request, rclone, boto3 UNSIGNED). See S3Server.cpp.
//

#ifndef FREEBUTTON_S3SERVER_H
#define FREEBUTTON_S3SERVER_H

class AsyncWebServer;

// Registers the handler. Must be called before serveStatic("/") so S3
// requests for "/" (ListBuckets) and "/<bucket>..." are not taken by the IDE.
void s3_register(AsyncWebServer& server);

// True when no S3 request holds a LittleFS file open (no PutObject upload,
// no GetObject transfer). A filesystem OTA only unmounts LittleFS then.
// async_tcp task only (like the handlers).
bool s3_idle();

#endif  // FREEBUTTON_S3SERVER_H
