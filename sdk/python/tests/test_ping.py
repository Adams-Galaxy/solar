from solar_remote.protocol import PingRequest, PingResponse


def test_ping_payloads_round_trip_exact_timing_fields() -> None:
    request = PingRequest(0x0102030405060708, 0x1112131415161718)
    assert request.encode().hex() == "08070605040302011817161514131211"

    response = PingResponse.decode(
        request.encode()
        + (100).to_bytes(8, "little")
        + (104).to_bytes(8, "little")
    )
    assert response.nonce == request.nonce
    assert response.host_monotonic_ns == request.host_monotonic_ns
    assert response.remote_send_us - response.remote_receive_us == 4
