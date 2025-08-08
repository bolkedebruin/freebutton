//
// Created by Bolke de Bruin on 18/07/2024.
//

#ifndef STREAMWRAPPERS_H
#define STREAMWRAPPERS_H

#include "ESPAsyncWebServer.h"

/*
 * Implementations of JSON output stream wrappers
 */
class AsyncJsonOutputStream {
public:
    typedef char Ch;
    AsyncJsonOutputStream(AsyncResponseStream* stream) : stream_(stream) {}

    void Put(Ch c) {
        stream_->write(&c, 1);
    }

    void Flush() {
        stream_->flush();
    }

private:
    AsyncResponseStream* stream_;
};

class FileOutputStream {
public:
    typedef char Ch;

    FileOutputStream(File& file) : file_(file) {}

    void Put(Ch c) {
        file_.write(c);
    }

    void Flush() {
        file_.flush();
    }

private:
    File& file_;
};

#endif //STREAMWRAPPERS_H
