// Anolis Runtime — Phase 2 Build Validation
// Purpose: Verify build system and protobuf linkage before implementing Phase 3

#include <iostream>
#include "protocol.pb.h"

int main(int argc, char** argv) {
    std::cerr << "anolis-runtime: Phase 2 build validation\n";

    // Verify protobuf linkage by creating a message
    anolis::deviceprovider::v0::Request req;
    req.set_request_id(1);

    std::cerr << "Protobuf linked successfully\n";
    std::cerr << "Request ID: " << req.request_id() << "\n";

    return 0;
}
