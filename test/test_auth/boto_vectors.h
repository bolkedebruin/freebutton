// Generated with botocore 1.43.126 (S3SigV4Auth / S3SigV4QueryAuth), time 2026-11-08T12:00:00Z,
// akid FBABCDEFGHIJKLMNOP23 secret abcdefghijABCDEFGHIJ0123456789klmnopqrst. Do not edit.
#pragma once
struct BotoVector { const char* name; const char* method; const char* target; const char* const* headers; const char* body; };
static const char* const b_reboot[] = {"Host", "192.168.1.50", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=160ef78856ef16566e5f79c68e657943ed14740348c27f9009e665e2be9f2395", "Content-Length", "0", nullptr};
static const char* const b_config[] = {"Host", "192.168.1.50", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=5ca638341f28065390d0d90aa7e3d8ce393105f343454783f48485d664ff0f30", nullptr};
static const char* const b_s3_put[] = {"Host", "192.168.1.50", "Content-Type", "application/javascript", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "c2d0a5e0790d97a015387a995c0d0b5eb3e88138466586fc980787c9b1731eb8", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date, Signature=e791df64f821377e66404620d4069012bce84f8d5d93e3bd8900db19d23dbc5a", "Content-Length", "11", nullptr};
static const char* const b_s3_list[] = {"Host", "192.168.1.50", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=64d267f86116670c44a1b4f4f2e170e640fb82cde32f3d20dda8ed751e6e92b8", nullptr};
static const char* const b_ws_presign[] = {"Host", "192.168.1.50", nullptr};
static const char* const b_presign_query[] = {"Host", "192.168.1.50:8080", nullptr};
static const BotoVector kBoto[] = {
    {"reboot", "POST", "/api/reboot", b_reboot, ""},
    {"config", "GET", "/config", b_config, ""},
    {"s3-put", "PUT", "/fs/scripts/main%20file%24.js", b_s3_put, "print('hi')"},
    {"s3-list", "GET", "/fs?list-type=2&prefix=scripts%2F&delimiter=%2F", b_s3_list, ""},
    {"ws-presign", "GET", "/ws/repl?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=FBABCDEFGHIJKLMNOP23%2F20261108%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261108T120000Z&X-Amz-Expires=300&X-Amz-SignedHeaders=host&X-Amz-Signature=c8fcccd0b1da0d1d0b6cfe9298ecc3e0e58e6cbd7c0628a794171a5350408f61", b_ws_presign, ""},
    {"presign-query", "GET", "/fs/a.txt?versionId=x%2Fy&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=FBABCDEFGHIJKLMNOP23%2F20261108%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261108T120000Z&X-Amz-Expires=300&X-Amz-SignedHeaders=host&X-Amz-Signature=83b1a733fc9e8a235cb72926b1335506be81554731a13b516992bb838752e298", b_presign_query, ""},
};
