import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
import logging

class MqttHandler(logging.Handler):
  def __init__(self, host='localhost', topic='/loggging', 
                 protocol=mqtt.MQTTv311):

    logging.Handler.__init__(self)

    self.topic = topic
    self.host = host
    self.protocol = protocol

  def emit(self,record):
    try:
      msg = self.format(record)
      publish.single(self.topic, msg, hostname=self.host, protocol=self.protocol)
  
      if record.exc_info:
         msg = "EXCEPTION: " + logging._defaultFormatter.formatException(record.exc_info)
         publish.single(self.topic, msg, hostname=self.host, protocol=self.protocol)

    except (KeyboardInterrupt, SystemExit):
      raise
    except:
      self.handleError(record)


