'use strict';
promise_test(
    () => {
      let val = new Uint8Array([1]);
      let promise;
      return setBluetoothFakeAdapter('FailingGATTOperationsAdapter')
          .then(
              () => requestDeviceWithKeyDown(
                  {filters: [{services: [errorUUID(0xA0)]}]}))
          .then(device => device.gatt.connect())
          .then(gattServer => gattServer.getPrimaryService(errorUUID(0xA0)))
          .then(service => service.getCharacteristic(errorUUID(0xA1)))
          .then(
              characteristic =>
                  characteristic.getDescriptor(user_description.name))
          .then(error_descriptor => {
            promise = assert_promise_rejects_with_message(
                error_descriptor.CALLS([readValue()|writeValue(val)]),
                new DOMException(
                    'GATT Server disconnected while performing a GATT operation.',
                    'NetworkError'));
            // Disconnect called to clear attributeInstanceMap and allow the
            // object to get garbage collected.
            error_descriptor.characteristic.service.device.gatt.disconnect();
          })
          .then(runGarbageCollection)
          .then(() => promise);
    },
    'Garbage Collection ran during a FUNCTION_NAME call that fails. ' +
        'Should not crash.');
