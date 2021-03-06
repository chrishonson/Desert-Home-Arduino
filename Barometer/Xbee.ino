void getDeviceParameters(){
  uint32_t addrl = 0;
  uint32_t addrh = 0;
  uint32_t daddrl = 0;
  uint32_t daddrh = 0;
  
  if(sendAtCommand((uint8_t *)"VR")){
    if (atResponse.getValueLength() != 2)
      Serial.println(F("Wrong length in VR response"));
    deviceFirmware = atResponse.getValue()[0] << 8;
    deviceFirmware += atResponse.getValue()[1];
  }
  if(sendAtCommand((uint8_t *)"AP")){
    if (atResponse.getValue()[0] != 0x02)
      Serial.println(F("Double Check the XBee AP setting, should be 2"));
  }
  if(sendAtCommand((uint8_t *)"AO")){
    if (atResponse.getValue()[0] != 0)
      Serial.println(F("Double Check the XBee A0 setting, should be 0"));
  }
  
  if(sendAtCommand((uint8_t *)"NI")){
    memset(deviceName, 0, sizeof(deviceName));
    for (int i = 0; i < atResponse.getValueLength(); i++) {
      deviceName[i] = (atResponse.getValue()[i]);
    }
    if (atResponse.getValueLength() == 0){
      Serial.println(F("Couldn't find a device name"));
    }
  }
  if(sendAtCommand((uint8_t *)"SH")){
    for (int i = 0; i < atResponse.getValueLength(); i++) {
      addrh = (addrh << 8) + atResponse.getValue()[i];
    }
  }
  if(sendAtCommand((uint8_t *)"SL")){
    for (int i = 0; i < atResponse.getValueLength(); i++) {
      addrl = (addrl << 8) + atResponse.getValue()[i];
    }
  }
  ThisDevice=XBeeAddress64(addrh,addrl);
  if(sendAtCommand((uint8_t *)"DH")){
    for (int i = 0; i < atResponse.getValueLength(); i++) {
      daddrh = (daddrh << 8) + atResponse.getValue()[i];
    }
  }
  if(sendAtCommand((uint8_t *)"DL")){
    for (int i = 0; i < atResponse.getValueLength(); i++) {
      daddrl = (daddrl << 8) + atResponse.getValue()[i];
    }
  }
  Controller=XBeeAddress64(daddrh,daddrl);
}

uint8_t frameID = 12;

boolean sendAtCommand(uint8_t *command) {
  while(1){
    frameID++;
    atRequest.setFrameId(frameID);
    atRequest.setCommand(command);
    // send the command
    xbee.send(atRequest);
    //Serial.println("sent command");
    // now wait a half second for the status response
    if (xbee.readPacket(500)) {
      // got a response!
  
      // should be an AT command response
      if (xbee.getResponse().getApiId() == AT_COMMAND_RESPONSE) {
        xbee.getResponse().getAtCommandResponse(atResponse);
  
        if (atResponse.isOk()) {
          //Serial.println("response OK");
          if (atResponse.getFrameId() == frameID){
            //Serial.print("Frame ID matched: ");
            //Serial.println(atResponse.getFrameId());
            return(true);
          }
          else {
            Serial.println("Frame Id did not match");
          }
        }
        else {
          Serial.print(F("Command returned error code: "));
          Serial.println(atResponse.getStatus(), HEX);
        }
      }
      else {
        Serial.print(F("Expected AT response but got "));
        Serial.println(xbee.getResponse().getApiId(), HEX);
      }
    } 
    else {
      // at command failed
      if (xbee.getResponse().isError()) {
        Serial.print(F("Error reading packet.  Error code: "));  
        Serial.println(xbee.getResponse().getErrorCode());
      } 
      else {
        Serial.println(F("No response from radio"));
      }
    }
  }
}


void sendXbee(const char* command){
  ZBTxRequest zbtx = ZBTxRequest(Destination, (uint8_t *)command, strlen(command));
  xbee.send(zbtx);
}

void sendStatusXbee(){
  float temperature = bmp.readTemperature()* 9/5 +32;
  float pressure = bmp.readSealevelPressure(altitude)/100.0;
  sprintf(Dbuf, "{\"%s\":{\"temperature\":\"%s\",\"pressure\":\"%s\",\"utime\":\"%ld\"}}\n", 
            deviceName, // originally read from the XBee
            dtostrf(temperature, 1, 1, t), // This is a text conversion of a float
            dtostrf(pressure, 1, 1, p),  // same as above
            now());  // current time in seconds
  Serial.print(Dbuf); // notice this is only for the serial port
  sendXbee(Dbuf);     // out to the XBee
  Serial.println("Pressure message sent");
}

void checkXbee(){
    // doing the read without a timer makes it non-blocking, so
    // you can do other stuff in loop() as well.
    xbee.readPacket();
    // so the read above will set the available up to 
    // work when you check it.
    if (xbee.getResponse().isAvailable()) {
      // got something
      if (xbee.getResponse().getApiId() == ZB_RX_RESPONSE) {
        // got a zb rx packet, the kind this code is looking for
        xbee.getResponse().getZBRxResponse(xbrx);
        processXbee((char *)xbrx.getData(), xbrx.getDataLength());
      }
      else if(xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE){
        xbee.getResponse().getZBTxStatusResponse(txStatus);
        if (txStatus.getDeliveryStatus() != SUCCESS) { // This means the far end didn't get it
          Serial.print(F("Transmit error "));
          Serial.println(txStatus.getDeliveryStatus(), HEX);
        }
      }
      else if(xbee.getResponse().getApiId() == ZB_IO_SAMPLE_RESPONSE){
          //Serial.println(F("I/O messsage"));
      }
      else {
        Serial.print("Got frame id: ");
        Serial.println(xbee.getResponse().getApiId(), HEX);
      }
    } 
    else if (xbee.getResponse().isError()) {
      // some kind of error happened, I put the stars in so
      // it could easily be found
      xbee.getResponse().getZBRxResponse(xbrx);
      XBeeAddress64 senderLongAddress = xbrx.getRemoteAddress64();
      print32Bits(senderLongAddress.getLsb());
      Serial.print(F(" XBee error code: "));
      switch(xbee.getResponse().getErrorCode()){
        case NO_ERROR:
          Serial.print(F("Shouldn't be here, no error "));
          break;
        case CHECKSUM_FAILURE:
          Serial.print(F("Bad Checksum "));
          break;
        case PACKET_EXCEEDS_BYTE_ARRAY_LENGTH:
          Serial.print(F("Packet too big "));
          break;
        case UNEXPECTED_START_BYTE:
          Serial.print(F("Bad start byte "));
          break;
        default:
          Serial.print(F("undefined "));
          break;
      }
      Serial.println(xbee.getResponse().getErrorCode(),DEC);
    }
    else {
      // This means we haven't gotten a complete XBee frame yet
    }
}

void processXbee(char* inbuf, int len){
  time_t temptime = 0;
  char* data;
  char buf[128];  // should be larger than max size of packet

  strncpy(buf, inbuf, min(len, sizeof(buf)));
  buf[min(len, sizeof(buf))] = 0;
  //  Serial.println(buf);
  if(!isalpha(*buf) && *buf != '?'){  // the first character is not alpha or '?'
//    strcpy_P(Dbuf,PSTR("bad starting character, ignoring"));
//    Serial.println(Dbuf);
    return;
  }
  if(strchr(buf, '\r') == 0){ // all my packets have to have a cr at the end.
//    strcpy_P(Dbuf,PSTR("partial packet, ignoring"));
//    Serial.println(Dbuf);
    return;
  }
  if((strstr_P(buf,PSTR("Time"))) != 0){
    Serial.println("Time Data");
    data = strtok(buf, ",");  // skip to the actual t_time value
    data = strtok(0, ",");   // get the t_time variable
    temptime = atol(data);
    // below is the trick go subtracting unsigned values that may go negative. 
    long difference = (long)(now() - temptime);
    if(abs(difference) >= 5){  // only if it matters
      Serial.println("Setting Time");
      Serial.print("Device time - actual time = ");
      Serial.print(difference);
      Serial.println(" seconds");
      setTime(temptime);
    }
  }
  // Notice how I compare the first field of the incoming commmand to the
  // device name I pulled from the XBee.  This keep this device from responding
  // to commands meant for other devices accidentally.
  else if (strcmp(strtok(buf, ","), deviceName) == 0){ // see if the message is really for this device
    char *nxtToken = strtok(0, ",\r");
    if (strcmp(nxtToken, "Status") == 0){
      Serial.println(F("Sending Status"));
      sendStatusXbee();
    }
    // The ability to broadcast is sometimes used to see what it going on
    // using an XBee plugged into a laptop to monitor the data.
    else if (strcmp(nxtToken, "SendBroadcast") == 0){
      Serial.println(F("Switching Address to Broadcast"));
      Destination = Broadcast;     // set XBee destination address to Broadcast for now
    }
    else if (strcmp(nxtToken, "SendController") == 0){
      Serial.println(F("Switching Address to Controller"));
      Destination = Controller;     // set XBee destination
    }
    else if (strcmp(nxtToken, "Reset") == 0){
      Serial.println(F("Commanded to do a reset"));
      resetFunc();  //This will reset the board and start all over.
    }
    else{
      Serial.print(deviceName);
      Serial.println(F(" bad command"));
    }
  }
  else if (strstr_P(buf,PSTR("Power")) != 0)
    Serial.println("Power Data");
  else if (strstr_P(buf,PSTR("Pool")) != 0)
    Serial.println("Pool Data");
  else if (strstr_P(buf,PSTR("Status")) != 0)
    Serial.println("Status Data");
  else if (strstr_P(buf,PSTR("AcidPump")) != 0)
    Serial.println("Acid Pump");
  else if (strstr_P(buf,PSTR("?")) != 0)
    Serial.println("Query House Controller");
  else if (strstr_P(buf,PSTR("Garage")) != 0)
    Serial.println("Garage Command");
  else {
    strcpy_P(Dbuf,PSTR("Undefined payload, ignoring"));
    Serial.println(buf);
  }
}

