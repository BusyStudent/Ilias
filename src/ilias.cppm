module;

#include <ilias.hpp>

export module ilias;

// Re-export
export namespace ilias {
    // Core
    using ilias::Result;
    using ilias::Err;
    using ilias::Buffer;
    using ilias::MutableBuffer;
    using ilias::makeBuffer;

    // Runtime
    using ilias::runtime::Executor;
    using ilias::EventLoop;
    using ilias::StackFrame;
    using ilias::Stacktrace;

    // Task module
    using ilias::Task;
    using ilias::TaskGroup;
    using ilias::TaskScope;
    using ilias::Generator;

    using ilias::spawn;
    using ilias::sleep;
    using ilias::whenAny;
    using ilias::whenAll;
    using ilias::finally;
    using ilias::unstoppable;
    using ilias::setTimeout;
    using ilias::scheduleOn;

    // Io module
    using ilias::IoGenerator;
    using ilias::IoContext;
    using ilias::IoResult;
    using ilias::IoError;
    using ilias::IoTask;
    using ilias::SystemError;

    // Io Buffer
    using ilias::StreamBuffer;
    using ilias::FixedStreamBuffer;
    using ilias::BufReader;
    using ilias::BufWriter;
    using ilias::BufStream;

    // Io concept
    using ilias::Readable;
    using ilias::Writable;
    using ilias::Stream;
    using ilias::ReadableMethod;
    using ilias::WritableMethod;
    using ilias::StreamMethod;

    // Net module
    using ilias::IPEndpoint;
    using ilias::IPAddress;
    using ilias::IPAddress4;
    using ilias::IPAddress6;
    using ilias::AddressInfo;
    using ilias::TcpListener;
    using ilias::TcpStream;
    using ilias::UdpSocket;
    using ilias::SocketView;
    using ilias::Socket;
    using ilias::PollEvent;
    using ilias::Poller;

    // Platform
    using ilias::PlatformContext;

#if defined(_WIN32)
    using ilias::IocpContext;
#endif // _WIN32

} // namespace ilias