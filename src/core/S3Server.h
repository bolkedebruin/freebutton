//
// S3Server - minimal S3-compatible API over LittleFS (path-style, one bucket
// "fs" = the LittleFS root), for the web IDE (@aws-sdk/client-s3) and S3 CLI
// tools (aws --no-sign-request, rclone, boto3 UNSIGNED). See S3Server.cpp.
//

#ifndef FREEBUTTON_S3SERVER_H
#define FREEBUTTON_S3SERVER_H

class AsyncWebServer;

// Registers the handler. Must be called before serveStatic("/") so S3
// requests for "/" (ListBuckets) and "/fs..." are not taken by the IDE.
void s3_register(AsyncWebServer& server);

#endif  // FREEBUTTON_S3SERVER_H
