// Generated with botocore 1.43.126 (S3SigV4Auth / S3SigV4QueryAuth), time 2026-11-08T12:00:00Z,
// akid FBABCDEFGHIJKLMNOP23 secret abcdefghijABCDEFGHIJ0123456789klmnopqrst. Do not edit.
#pragma once
struct BotoVector { const char* name; const char* method; const char* target; const char* const* headers; const char* body; };
static const char* const b_reboot[] = {"Host", "192.168.1.50", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=6ecc316eb91bf8181836fb2c6aa75f3d509d8899e0e7ed053cfe486f2f56d7e9", "Content-Length", "0", nullptr};
static const char* const b_config[] = {"Host", "192.168.1.50", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=a6f7863474c1a0ee0d13569c392464728443ee471aee24b4a2dcd185c13724cc", nullptr};
static const char* const b_s3_put[] = {"Host", "192.168.1.50", "Content-Type", "application/javascript", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "c2d0a5e0790d97a015387a995c0d0b5eb3e88138466586fc980787c9b1731eb8", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=content-type;host;x-amz-content-sha256;x-amz-date, Signature=fb3f6021ce21d852dfb358e34caf00fc48578074d8e02c64fec5401604403f52", "Content-Length", "11", nullptr};
static const char* const b_s3_list[] = {"Host", "192.168.1.50", "X-Amz-Date", "20261108T120000Z", "X-Amz-Content-SHA256", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Authorization", "AWS4-HMAC-SHA256 Credential=FBABCDEFGHIJKLMNOP23/20261108/us-east-1/s3/aws4_request, SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=7d90e80d3135d88180527cb8889a5946ddbb876de0d51c962a2a47b80ee8c36d", nullptr};
static const char* const b_ws_presign[] = {"Host", "192.168.1.50", nullptr};
static const char* const b_presign_query[] = {"Host", "192.168.1.50:8080", nullptr};
static const BotoVector kBoto[] = {
    {"reboot", "POST", "/api/reboot", b_reboot, ""},
    {"config", "GET", "/config", b_config, ""},
    {"s3-put", "PUT", "/fs/scripts/main%20file%24.js", b_s3_put, "print('hi')"},
    {"s3-list", "GET", "/fs?list-type=2&prefix=scripts%2F&delimiter=%2F", b_s3_list, ""},
    {"ws-presign", "GET", "/ws/repl?X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=FBABCDEFGHIJKLMNOP23%2F20261108%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261108T120000Z&X-Amz-Expires=300&X-Amz-SignedHeaders=host&X-Amz-Signature=cf031b92665d9f9134b675252b0fd884207aedd10aa73faa787536ed6a5c0ed6", b_ws_presign, ""},
    {"presign-query", "GET", "/fs/a.txt?versionId=x%2Fy&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=FBABCDEFGHIJKLMNOP23%2F20261108%2Fus-east-1%2Fs3%2Faws4_request&X-Amz-Date=20261108T120000Z&X-Amz-Expires=300&X-Amz-SignedHeaders=host&X-Amz-Signature=80e59ec29cfbaa9af94ef854b0f94734921ed9162ee5cc42280d0195b8ad8837", b_presign_query, ""},
};
