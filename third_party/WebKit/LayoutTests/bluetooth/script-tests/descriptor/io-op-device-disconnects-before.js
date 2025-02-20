'use strict';
promise_test(() => {
  let val = new Uint8Array([1]);
  return setBluetoothFakeAdapter('DisconnectingHealthThermometerAdapter')
      .then(() => requestDeviceWithKeyDown({
              filters: [{services: ['health_thermometer']}],
              optionalServices: [request_disconnection_service_uuid]
            }))
      .then(device => device.gatt.connect())
      .then(gattServer => {
        let measurement_interval;
        return gattServer.getPrimaryService('health_thermometer')
            .then(ht => ht.getCharacteristic('measurement_interval'))
            .then(
                characteristic =>
                    characteristic.getDescriptor(user_description.name))
            .then(d => user_description = d)
            .then(() => get_request_disconnection(gattServer))
            .then(requestDisconnection => requestDisconnection())
            .then(
                () => assert_promise_rejects_with_message(
                    user_description.CALLS([readValue()|writeValue(val)]),
                    new DOMException(
                        'GATT Server is disconnected. Cannot perform GATT operations.',
                        'NetworkError')));
      });
}, 'Device disconnects before FUNCTION_NAME. Reject with NetworkError.');
