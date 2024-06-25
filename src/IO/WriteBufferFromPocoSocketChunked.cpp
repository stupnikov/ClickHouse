#include <IO/WriteBufferFromPocoSocketChunked.h>
#include <Common/logger_useful.h>
#include <IO/NetUtils.h>


namespace
{

template <typename T>
const T & setValue(T * typed_ptr, std::type_identity_t<T> val)
{
    memcpy(typed_ptr, &val, sizeof(T));
    return *typed_ptr;
}

}

namespace DB
{

void WriteBufferFromPocoSocketChunked::enableChunked()
{
    chunked = true;
    /// Initialize next chunk
    chunk_size_ptr = reinterpret_cast<decltype(chunk_size_ptr)>(pos);
    pos += std::min(available(), sizeof(*chunk_size_ptr));
    /// Pretend finishChunk() was just called to prevent sending empty chunk if finishChunk() called immediately
    last_finish_chunk = chunk_size_ptr;
}

void WriteBufferFromPocoSocketChunked::finishChunk()
{
    if (!chunked)
        return;

    if (pos <= reinterpret_cast<Position>(chunk_size_ptr) + sizeof(*chunk_size_ptr))
    {
        /// Prevent duplicate finish chunk (and finish chunk right after enableChunked())
        if (chunk_size_ptr == last_finish_chunk)
            return;

        /// If current chunk is empty it means we are finishing a chunk previously sent by next(),
        /// we want to convert current chunk header into end-of-chunk marker and initialize next chunk.
        /// We don't need to worry about if it's the end of the buffer because next() always sends the whole buffer
        /// so it should be a beginning of the buffer.

        chassert(reinterpret_cast<Position>(chunk_size_ptr) == working_buffer.begin());

        setValue(chunk_size_ptr, 0);
        /// Initialize next chunk
        chunk_size_ptr = reinterpret_cast<decltype(chunk_size_ptr)>(pos);
        pos += std::min(available(), sizeof(*chunk_size_ptr));

        last_finish_chunk = chunk_size_ptr;

        return;
    }

    /// Previously finished chunk wasn't sent yet
    if (last_finish_chunk == chunk_size_ptr)
    {
        chunk_started = false;
        LOG_TEST(log, "{} -> {} Chunk send ended.", ourAddress().toString(), peerAddress().toString());
    }

    /// Fill up current chunk size
    setValue(chunk_size_ptr, toLittleEndian(static_cast<UInt32>(pos - reinterpret_cast<Position>(chunk_size_ptr) - sizeof(*chunk_size_ptr))));

    if (!chunk_started)
        LOG_TEST(log, "{} -> {} Chunk send started. Message {}, size {}",
                ourAddress().toString(), peerAddress().toString(),
                static_cast<unsigned int>(*(reinterpret_cast<char *>(chunk_size_ptr) + sizeof(*chunk_size_ptr))),
                *chunk_size_ptr);
    else
    {
        chunk_started = false;
        LOG_TEST(log, "{} -> {} Chunk send continued. Size {}", ourAddress().toString(), peerAddress().toString(), *chunk_size_ptr);
    }

    LOG_TEST(log, "{} -> {} Chunk send ended.", ourAddress().toString(), peerAddress().toString());

    if (available() < sizeof(*chunk_size_ptr))
    {
        finishing = available();
        pos += available();
        chunk_size_ptr = reinterpret_cast<decltype(chunk_size_ptr)>(pos);
        return;
    }

    /// Buffer end-of-chunk
    setValue(reinterpret_cast<decltype(chunk_size_ptr)>(pos), 0);
    pos += sizeof(*chunk_size_ptr);
    /// Initialize next chunk
    chunk_size_ptr = reinterpret_cast<decltype(chunk_size_ptr)>(pos);
    pos += std::min(available(), sizeof(*chunk_size_ptr));

    last_finish_chunk = chunk_size_ptr;
}

WriteBufferFromPocoSocketChunked::~WriteBufferFromPocoSocketChunked()
{
    try
    {
        finalize();
    }
    catch (...)
    {
        tryLogCurrentException(__PRETTY_FUNCTION__);
    }
}

void WriteBufferFromPocoSocketChunked::nextImpl()
{
    if (!chunked)
    {
        WriteBufferFromPocoSocket::nextImpl();
        return;
    }

    /// next() after finishChunk at the end of the buffer
    if (finishing < sizeof(*chunk_size_ptr))
    {
        pos -= finishing;
        /// Send current chunk
        WriteBufferFromPocoSocket::nextImpl();
        /// Send end-of-chunk directly
        UInt32 s = 0;
        socketSendBytes(reinterpret_cast<const char *>(&s), sizeof(s));

        finishing = sizeof(*chunk_size_ptr);

        /// Initialize next chunk
        chunk_size_ptr = reinterpret_cast<decltype(chunk_size_ptr)>(working_buffer.begin());
        nextimpl_working_buffer_offset = sizeof(*chunk_size_ptr);

        last_finish_chunk = chunk_size_ptr;

        return;
    }

    /// Prevent sending empty chunk
    if (offset() == sizeof(*chunk_size_ptr))
    {
        nextimpl_working_buffer_offset = sizeof(*chunk_size_ptr);
        return;
    }

    /// Finish chunk at the end of the buffer
    if (working_buffer.end() - reinterpret_cast<Position>(chunk_size_ptr) <= static_cast<std::ptrdiff_t>(sizeof(*chunk_size_ptr)))
    {
        pos = reinterpret_cast<Position>(chunk_size_ptr);
        /// Send current chunk
        WriteBufferFromPocoSocket::nextImpl();
        /// Initialize next chunk
        chunk_size_ptr = reinterpret_cast<decltype(chunk_size_ptr)>(working_buffer.begin());
        nextimpl_working_buffer_offset = sizeof(*chunk_size_ptr);

        last_finish_chunk = nullptr;

        return;
    }

    bool initialize_last_finish_chunk = false;
    if (pos - reinterpret_cast<Position>(chunk_size_ptr) == sizeof(*chunk_size_ptr)) // next() after finishChunk
    {
        pos -= sizeof(*chunk_size_ptr);
        initialize_last_finish_chunk = true;
    }
    else // fill up current chunk size
    {
        setValue(chunk_size_ptr, toLittleEndian(static_cast<UInt32>(pos - reinterpret_cast<Position>(chunk_size_ptr) - sizeof(*chunk_size_ptr))));
        if (!chunk_started)
        {
            chunk_started = true;
            LOG_TEST(log, "{} -> {} Chunk send started. Message {}, size {}",
                    ourAddress().toString(), peerAddress().toString(),
                    static_cast<unsigned int>(*(reinterpret_cast<char *>(chunk_size_ptr) + sizeof(*chunk_size_ptr))),
                    *chunk_size_ptr);
        }
        else
            LOG_TEST(log, "{} -> {} Chunk send continued. Size {}", ourAddress().toString(), peerAddress().toString(), *chunk_size_ptr);
    }
    /// Send current chunk
    WriteBufferFromPocoSocket::nextImpl();
    /// Initialize next chunk
    chunk_size_ptr = reinterpret_cast<decltype(chunk_size_ptr)>(working_buffer.begin());
    nextimpl_working_buffer_offset = sizeof(*chunk_size_ptr);

    last_finish_chunk = initialize_last_finish_chunk ? chunk_size_ptr : nullptr;
}

void WriteBufferFromPocoSocketChunked::finalizeImpl()
{
    if (offset() == sizeof(*chunk_size_ptr))
        pos -= sizeof(*chunk_size_ptr);
    WriteBufferFromPocoSocket::finalizeImpl();
}

}
