#include "androidauto/ChannelSupport.h"
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <utility>

namespace headunit {
// A send promise on `strand` that calls `onSent` once the message is out and `onError` when sending failed.
aasdk::channel::SendPromise::Pointer MakeSendPromise(boost::asio::io_context::strand& strand, std::function<void()> onSent,
    std::function<void(const aasdk::error::Error&)> onError)
{
    auto promise = aasdk::channel::SendPromise::defer(strand);
    promise->then(std::move(onSent), [onError = std::move(onError)](const aasdk::error::Error& error) { onError(error); });
    return promise;
}

// The answer to the phone opening a channel: accepted.
aap_protobuf::service::control::message::ChannelOpenResponse SuccessfulOpenResponse()
{
    aap_protobuf::service::control::message::ChannelOpenResponse response;
    response.set_status(aap_protobuf::shared::STATUS_SUCCESS);
    return response;
}

// The answer to a media channel setup: ready, one unacknowledged frame at a time, the first advertised configuration.
aap_protobuf::service::media::shared::message::Config ReadyMediaConfig()
{
    aap_protobuf::service::media::shared::message::Config response;
    response.set_status(aap_protobuf::service::media::shared::message::Config::STATUS_READY);
    response.set_max_unacked(1);
    response.add_configuration_indices(0);
    return response;
}

// Whether a channel error is only the session shutting down (not worth reporting).
bool IsAbortError(const aasdk::error::Error& error)
{
    return error.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED;
}
}
