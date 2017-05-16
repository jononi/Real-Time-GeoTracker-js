#-------------------------------------------------------------------------#
#
#  Charlotte IOT - Alexa to Particle IO Gateway for Fan and Lights    
#    written by: Jeremy Proffitt - Licensed for non commercial use only
#
#-------------------------------------------------------------------------#
#
#  NOTE: When you make changes to this script, you likely have to force
#        a rediscovery of Home Automation devices in the Alexa App.
#
#  To configure, change the following variables:

#Alexs device names
lightName = "Spare Bedroom";
fanName = "Fan";
spareOnOff1Name = "Back Door";
spareOnOff2Name = "Back Room";

#Particle Information:
particleToken = "**INSERT YOUR TOKEN HERE**";
particleDeviceId = "**INSERT YOUR DEVICE HERE**";
particleLightFunction = "setLight";
particleFanFunction = "setFan";
particleSpareOnOff1Function = "setOutput1";
particleSpareOnOff2Function = "setOutput2";

#configuration for Alexa
#  set the device type below using the below Enum.
from enum import Enum
class DeviceType(Enum):
    OnOff = 1
    Percent = 2
    Color = 3
    Lock = 4
    Disabled = 5

lightType = DeviceType.Color; 
fanType = DeviceType.Percent;  
spareOnOff1Type = DeviceType.Lock;
spareOnOff2Type = DeviceType.Color;


####################################################################################################
#
#                 DO NOT CHANGE ANYTHING BELOW THIS LINE.
#
####################################################################################################

import urllib
import json
import urllib.request
import urllib.parse

def lambda_handler(event, context):
    #sumoLog(event, context);
    access_token = event['payload']['accessToken']

    if event['header']['namespace'] == 'Alexa.ConnectedHome.Discovery':
        return handleDiscovery(context, event)

    elif event['header']['namespace'] == 'Alexa.ConnectedHome.Control':
        return handleControl(context, event)

def handleDiscovery(context, event):
    payload = ''

    header = {
        "namespace": "Alexa.ConnectedHome.Discovery",
        "name": "DiscoverAppliancesResponse",
        "payloadVersion": "2"
        }
        
    if event['header']['name'] == 'DiscoverAppliancesRequest':
        payload = {
            "discoveredAppliances":[
                {
                    "applianceId":particleLightFunction,
                    "manufacturerName":"CharlotteIOT",
                    "modelName":"IOT",
                    "version":"1",
                    "friendlyName":lightName,
                    "friendlyDescription":lightName,
                    "isReachable":True,
                    "actions":[
                        "turnOn",
                        "turnOff"
                    ],
                    "additionalApplianceDetails":{
                        "extraDetail1":"There are no extra details."
                    }
                },
                {
                    "applianceId":particleFanFunction,
                    "manufacturerName":"CharlotteIOT",
                    "modelName":"IOT",
                    "version":"1",
                    "friendlyName":fanName,
                    "friendlyDescription":fanName,
                    "isReachable":True,
                    "actions":[
                        "turnOn",
                        "turnOff"
                    ],
                    "additionalApplianceDetails":{
                        "extraDetail1":"There are no extra details."
                    }
                },
                {
                    "applianceId":particleSpareOnOff1Function,
                    "manufacturerName":"CharlotteIOT",
                    "modelName":"IOT",
                    "version":"1",
                    "friendlyName":spareOnOff1Name,
                    "friendlyDescription":spareOnOff1Name,
                    "isReachable":True,
                    "actions":[
                        "turnOn",
                        "turnOff"
                    ],
                    "additionalApplianceDetails":{
                        "extraDetail1":"There are no extra details."
                    }
                },
                {
                    "applianceId":particleSpareOnOff2Function,
                    "manufacturerName":"CharlotteIOT",
                    "modelName":"IOT",
                    "version":"1",
                    "friendlyName":spareOnOff2Name,
                    "friendlyDescription":spareOnOff2Name,
                    "isReachable":True,
                    "actions":[
                        "turnOn",
                        "turnOff"
                    ],
                    "additionalApplianceDetails":{
                        "extraDetail1":"There are no extra details."
                    }
                }
            ]
        }
        
        if spareOnOff2Type == DeviceType.Disabled:
            del payload['discoveredAppliances'][3];
        elif spareOnOff2Type == DeviceType.Lock:
            payload['discoveredAppliances'][3]['actions'].remove("turnOn");
            payload['discoveredAppliances'][3]['actions'].remove("turnOff");
            payload['discoveredAppliances'][3]['actions'].append("setLockState");
        elif spareOnOff2Type == DeviceType.Percent or spareOnOff2Type == DeviceType.Color:
            payload['discoveredAppliances'][3]['actions'].append("decrementPercentage");
            payload['discoveredAppliances'][3]['actions'].append("incrementPercentage");
            payload['discoveredAppliances'][3]['actions'].append("setPercentage");
        if spareOnOff2Type == DeviceType.Color:
            payload['discoveredAppliances'][3]['actions'].append("setColor");
        
            
        if spareOnOff1Type == DeviceType.Disabled:
            del payload['discoveredAppliances'][2];
        elif spareOnOff1Type == DeviceType.Lock:
            payload['discoveredAppliances'][2]['actions'].remove("turnOn");
            payload['discoveredAppliances'][2]['actions'].remove("turnOff");
            payload['discoveredAppliances'][2]['actions'].append("setLockState");
        elif spareOnOff1Type == DeviceType.Percent or spareOnOff1Type == DeviceType.Color:
            payload['discoveredAppliances'][2]['actions'].append("decrementPercentage");
            payload['discoveredAppliances'][2]['actions'].append("incrementPercentage");
            payload['discoveredAppliances'][2]['actions'].append("setPercentage");
        if spareOnOff1Type == DeviceType.Color:
            payload['discoveredAppliances'][2]['actions'].append("setColor");
        if spareOnOff1Type == DeviceType.Lock:
            payload['discoveredAppliances'][2]['actions'].append("setLockState");
        
        if fanType == DeviceType.Disabled:
            del payload['discoveredAppliances'][1];
        elif fanType == DeviceType.Lock:
            payload['discoveredAppliances'][1]['actions'].remove("turnOn");
            payload['discoveredAppliances'][1]['actions'].remove("turnOff");
            payload['discoveredAppliances'][1]['actions'].append("setLockState");
        elif fanType == DeviceType.Percent or spareOnOff2Type == DeviceType.Color:
            payload['discoveredAppliances'][1]['actions'].append("decrementPercentage");
            payload['discoveredAppliances'][1]['actions'].append("incrementPercentage");
            payload['discoveredAppliances'][1]['actions'].append("setPercentage");
        if fanType == DeviceType.Color:
            payload['discoveredAppliances'][1]['actions'].append("setColor");       
        if spareOnOff1Type == DeviceType.Lock:
            payload['discoveredAppliances'][1]['actions'].append("setLockState");
            
        if lightType == DeviceType.Disabled:
            del payload['discoveredAppliances'][0];
        elif lightType == DeviceType.Lock:
            payload['discoveredAppliances'][0]['actions'].remove("turnOn");
            payload['discoveredAppliances'][0]['actions'].remove("turnOff");
            payload['discoveredAppliances'][0]['actions'].append("setLockState");
        elif lightType == DeviceType.Percent or lightType == DeviceType.Color:
            payload['discoveredAppliances'][0]['actions'].append("decrementPercentage");
            payload['discoveredAppliances'][0]['actions'].append("incrementPercentage");
            payload['discoveredAppliances'][0]['actions'].append("setPercentage");
        if lightType == DeviceType.Color:
            payload['discoveredAppliances'][0]['actions'].append("setColor");       
            
            
    print(json.dumps(payload));
    return { 'header': header, 'payload': payload }

def handleControl(context, event):
    deviceId = event['payload']['appliance']['applianceId'];
    messageId = event['header']['messageId'];
    responseName = '';
    payload = { };
    
    print("handleControl Called.");
    print(deviceId);
    print(messageId);
    
    #Turn On
    if event['header']['name'] == 'TurnOnRequest':
        responseName = 'TurnOnConfirmation';
        sendToParticle(deviceId, "on");
        
    #TurnOff
    elif event['header']['name'] == 'TurnOffRequest':
        responseName = 'TurnOffConfirmation';
        sendToParticle(deviceId, "off");
    
    #setPercent
    elif event['header']['name'] == 'SetPercentageRequest':
        responseName = 'SetPercentageConfirmation';
        percent = event['payload']['percentageState']['value']
        sendToParticle(deviceId, percent);
    
    #decreasePercent
    elif event['header']['name'] == 'DecrementPercentageRequest':
        responseName = 'DecrementPercentageConfirmation';
        percent = event['payload']['deltaPercentage']['value']
        sendToParticle(deviceId, '-' + str(percent));
        
    #increasePercent
    elif event['header']['name'] == 'IncrementPercentageRequest':
        responseName = 'IncrementPercentageConfirmation';
        percent = event['payload']['deltaPercentage']['value']
        sendToParticle(deviceId, '+' + str(percent));
       
    #Color
    elif event['header']['name'] == 'SetColorRequest':
        print(event);
        responseName = 'SetColorConfirmation';
        hue = event['payload']['color']['hue'];
        saturation = event['payload']['color']['saturation'];
        brightness = event['payload']['color']['brightness'];
        sendToParticle(deviceId, "color:" + str(hue / 360 * 255) + ":" + str(saturation * 255) + ":" + str(brightness * 255));
        payload = {
            "achievedState": 
                {
                "color": 
                    {
                    "hue": 0.0,
                    "saturation": 1.0000,
                    "brightness": 1.0000
                    }
                }
            }
        
    #Lock/Unlock
    elif event['header']['name'] == 'SetLockStateRequest':
        responseName = 'TurnOnConfirmation';
        lockState = event['payload']['lockState']
        sendToParticle(deviceId, lockState);
        payload = {
            "lockState": lockState
        }
        
    #HealthCheck
    elif event['header']['name'] == 'HealthCheckRequest':
        responseName = 'HealthCheckResponse';
        sendToParticle(deviceId, "healthcheck");
        payload = {
            "description": "The system is currently healthy",
            "isHealthy": "true"
        }
    
    
    
    
    header = {
            "namespace":"Alexa.ConnectedHome.Control",
            "name":responseName,
            "payloadVersion":"2",
            "messageId": messageId
            }
    return { 'header': header, 'payload': payload }

def sendToParticle(function, value):
    print('sendToParticle({0}, {1})'.format(function, value));
    url = "https://api.particle.io/v1/devices/" + particleDeviceId + "/" + function;
    print('url: {0}'.format(url));
    body = urllib.parse.urlencode({'arg' : value , 'access_token' : particleToken});
    data = body.encode('ascii');
    urllib.request.urlopen(url, data=data);
    return;
    
