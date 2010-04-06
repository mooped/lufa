/*
             LUFA Library
     Copyright (C) Dean Camera, 2010.
              
  dean [at] fourwalledcubicle [dot] com
      www.fourwalledcubicle.com
*/

/*
  Copyright 2010  Dean Camera (dean [at] fourwalledcubicle [dot] com)

  Permission to use, copy, modify, distribute, and sell this 
  software and its documentation for any purpose is hereby granted
  without fee, provided that the above copyright notice appear in 
  all copies and that both that the copyright notice and this
  permission notice and warranty disclaimer appear in supporting 
  documentation, and that the name of the author not be used in 
  advertising or publicity pertaining to distribution of the 
  software without specific, written prior permission.

  The author disclaim all warranties with regard to this
  software, including all implied warranties of merchantability
  and fitness.  In no event shall the author be liable for any
  special, indirect or consequential damages or any damages
  whatsoever resulting from loss of use, data or profits, whether
  in an action of contract, negligence or other tortious action,
  arising out of or in connection with the use or performance of
  this software.
*/

#define  INCLUDE_FROM_BLUETOOTH_ACLPACKETS_C
#include "BluetoothACLPackets.h"

void Bluetooth_ACLTask(void)
{
	Bluetooth_ProcessACLPackets();
	
	for (uint8_t i = 0; i < BLUETOOTH_MAX_OPEN_CHANNELS; i++)
	{
		Bluetooth_Channel_t* ChannelData = &Bluetooth_Connection.Channels[i];
	
		bool MustSendConfigReq = true;
	
		switch (ChannelData->State)
		{
			case Channel_Config_WaitConfig:
				ChannelData->State = Channel_Config_WaitReqResp;
				break;
			case Channel_Config_WaitSendConfig:
				ChannelData->State = Channel_Config_WaitResp;
				break;
			default:
				MustSendConfigReq  = false;
				break;
		}
		
		if (MustSendConfigReq)
		{
			BT_ACL_Header_t              ACLPacketHeader;
			BT_DataPacket_Header_t       DataHeader;
			BT_Signal_Header_t           SignalCommandHeader;
			BT_Signal_ConfigurationReq_t ConfigurationRequest;

			ACLPacketHeader.ConnectionHandle     = Bluetooth_Connection.ConnectionHandle;
			ACLPacketHeader.DataLength           = sizeof(DataHeader) + sizeof(SignalCommandHeader) + sizeof(ConfigurationRequest);
			DataHeader.PayloadLength             = sizeof(SignalCommandHeader) + sizeof(ConfigurationRequest);
			DataHeader.DestinationChannel        = BT_CHANNEL_SIGNALING;
			SignalCommandHeader.Code             = BT_SIGNAL_CONFIGURATION_REQUEST;
			SignalCommandHeader.Identifier       = ++Bluetooth_Connection.SignallingIdentifier;
			SignalCommandHeader.Length           = sizeof(ConfigurationRequest);
			
			ConfigurationRequest.DestinationChannel = ChannelData->RemoteNumber;
			ConfigurationRequest.Flags              = 0;

			Pipe_SelectPipe(BLUETOOTH_DATA_OUT_PIPE);
			Pipe_Unfreeze();
			
			Pipe_Write_Stream_LE(&ACLPacketHeader, sizeof(ACLPacketHeader));
			Pipe_Write_Stream_LE(&DataHeader, sizeof(DataHeader));
			Pipe_Write_Stream_LE(&SignalCommandHeader, sizeof(SignalCommandHeader));
			Pipe_Write_Stream_LE(&ConfigurationRequest, sizeof(ConfigurationRequest));

			Pipe_Freeze();

			#if (ACL_DEBUG_LEVEL > 1)
			BT_ACL_DEBUG("Packet Sent", NULL);
			BT_ACL_DEBUG("-- Connection Handle: 0x%04X", (ACLPacketHeader.ConnectionHandle & 0x0FFF));
			BT_ACL_DEBUG("-- Data Length: 0x%04X", ACLPacketHeader.DataLength);
			BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DataHeader.DestinationChannel);
			BT_ACL_DEBUG("-- Payload Length: 0x%04X", DataHeader.PayloadLength);			
			#endif
			#if (ACL_DEBUG_LEVEL > 0)
			BT_ACL_DEBUG(">> L2CAP Configuration Request", NULL);
			#endif
			#if (ACL_DEBUG_LEVEL > 1)
			BT_ACL_DEBUG("-- Destination Channel: 0x%04X", ConfigurationRequest.DestinationChannel);
			#endif
		}
	}
}

static void Bluetooth_ProcessACLPackets(void)
{
	BT_ACL_Header_t        ACLPacketHeader;
	BT_DataPacket_Header_t DataHeader;

	Pipe_SelectPipe(BLUETOOTH_DATA_IN_PIPE);
	Pipe_Unfreeze();
	
	if (!(Pipe_IsReadWriteAllowed()))
	{
		Pipe_Freeze();
		return;
	}
	  
	Pipe_Read_Stream_LE(&ACLPacketHeader, sizeof(ACLPacketHeader));
	Pipe_Read_Stream_LE(&DataHeader, sizeof(DataHeader));

	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("Packet Received", NULL);
	BT_ACL_DEBUG("-- Connection Handle: 0x%04X", (ACLPacketHeader.ConnectionHandle & 0x0FFF));
	BT_ACL_DEBUG("-- Data Length: 0x%04X", ACLPacketHeader.DataLength);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DataHeader.DestinationChannel);
	BT_ACL_DEBUG("-- Payload Length: 0x%04X", DataHeader.PayloadLength);
	#endif

	if (DataHeader.DestinationChannel == BT_CHANNEL_SIGNALING)
	{
		BT_Signal_Header_t SignalCommandHeader;
		Pipe_Read_Stream_LE(&SignalCommandHeader, sizeof(SignalCommandHeader));

		switch (SignalCommandHeader.Code)
		{
			case BT_SIGNAL_CONNECTION_REQUEST:
				Bluetooth_Signal_ConnectionReq(&ACLPacketHeader, &DataHeader, &SignalCommandHeader);
				break;
			case BT_SIGNAL_CONFIGURATION_REQUEST:
				Bluetooth_Signal_ConfigurationReq(&ACLPacketHeader, &DataHeader, &SignalCommandHeader);
				break;
			case BT_SIGNAL_DISCONNECTION_REQUEST:
				Bluetooth_Signal_DisconnectionReq(&ACLPacketHeader, &DataHeader, &SignalCommandHeader);
				break;			
			case BT_SIGNAL_ECHO_REQUEST:
				Bluetooth_Signal_EchoReq(&ACLPacketHeader, &DataHeader, &SignalCommandHeader);
				break;
			case BT_SIGNAL_INFORMATION_REQUEST:
				Bluetooth_Signal_InformationReq(&ACLPacketHeader, &DataHeader, &SignalCommandHeader);
				break;
			default:
				#if (ACL_DEBUG_LEVEL > 0)
				BT_ACL_DEBUG("<< Unknown Signaling Command 0x%02X", SignalCommandHeader.Code);
				#endif
	
				Pipe_Discard_Stream(ACLPacketHeader.DataLength);
				Pipe_ClearIN();		
				Pipe_Freeze();
				break;
		}
	}
	else
	{
		Bluetooth_PacketReceived(&DataHeader.PayloadLength, Bluetooth_GetChannelData(DataHeader.DestinationChannel, true));
	
		Pipe_SelectPipe(BLUETOOTH_DATA_IN_PIPE);
		Pipe_Discard_Stream(DataHeader.PayloadLength);
		Pipe_ClearIN();
		Pipe_Freeze();
	}
}

uint8_t Bluetooth_SendPacket(uint8_t* Data, uint16_t DataLen, Bluetooth_Channel_t* Channel)
{
	BT_ACL_Header_t        ACLPacketHeader;
	BT_DataPacket_Header_t DataHeader;

	if (Bluetooth_Connection.IsConnected)
	  return BT_SENDPACKET_NotConnected;

	if (Channel->State != Channel_Open)
	  return BT_SENDPACKET_ChannelNotOpen;

	ACLPacketHeader.ConnectionHandle      = Bluetooth_Connection.ConnectionHandle;
	ACLPacketHeader.DataLength            = sizeof(DataHeader) + DataLen;
	DataHeader.DestinationChannel         = Channel->RemoteNumber;
	DataHeader.PayloadLength              = DataLen;

	Pipe_SelectPipe(BLUETOOTH_DATA_OUT_PIPE);
	Pipe_Unfreeze();
	
	Pipe_Write_Stream_LE(&ACLPacketHeader, sizeof(ACLPacketHeader));
	Pipe_Write_Stream_LE(&DataHeader, sizeof(DataHeader));
	Pipe_Write_Stream_LE(Data, DataLen);

	Pipe_Freeze();
	
	return BT_SENDPACKET_NoError;
}

static inline void Bluetooth_Signal_ConnectionReq(BT_ACL_Header_t* ACLPacketHeader,
                                                  BT_DataPacket_Header_t* DataHeader,
                                                  BT_Signal_Header_t* SignalCommandHeader)
{
	BT_Signal_ConnectionReq_t ConnectionRequest;
	
	Pipe_Read_Stream_LE(&ConnectionRequest, sizeof(ConnectionRequest));

	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG("<< L2CAP Connection Request", NULL);
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- PSM: 0x%04X", ConnectionRequest.PSM);
	BT_ACL_DEBUG("-- Source Channel: 0x%04X", ConnectionRequest.SourceChannel);
	#endif
	
	Pipe_ClearIN();
	Pipe_Freeze();
	Pipe_SelectPipe(BLUETOOTH_DATA_OUT_PIPE);
	Pipe_Unfreeze();
	
	BT_Signal_ConnectionResp_t ConnectionResponse;

	ACLPacketHeader->DataLength           = sizeof(*DataHeader) + sizeof(*SignalCommandHeader) + sizeof(ConnectionResponse);
	DataHeader->PayloadLength             = sizeof(*SignalCommandHeader) + sizeof(ConnectionResponse);
	DataHeader->DestinationChannel        = BT_CHANNEL_SIGNALING;
	SignalCommandHeader->Code             = BT_SIGNAL_CONNECTION_RESPONSE;
	SignalCommandHeader->Length           = sizeof(ConnectionResponse);
	
	Bluetooth_Channel_t* ChannelData      = Bluetooth_InitChannelData(ConnectionRequest.SourceChannel, ConnectionRequest.PSM);
	
	ConnectionResponse.Result             = (ChannelData == NULL) ? BT_CONNECTION_REFUSED_RESOURCES : BT_CONNECTION_SUCCESSFUL;
	ConnectionResponse.DestinationChannel = ChannelData->LocalNumber;
	ConnectionResponse.SourceChannel      = ChannelData->RemoteNumber;
	ConnectionResponse.Status             = 0x00;

	Pipe_Write_Stream_LE(ACLPacketHeader, sizeof(*ACLPacketHeader));
	Pipe_Write_Stream_LE(DataHeader, sizeof(*DataHeader));
	Pipe_Write_Stream_LE(SignalCommandHeader, sizeof(*SignalCommandHeader));
	Pipe_Write_Stream_LE(&ConnectionResponse, sizeof(ConnectionResponse));
	
	Pipe_ClearOUT();		
	Pipe_Freeze();
	
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("Packet Sent", NULL);
	BT_ACL_DEBUG("-- Connection Handle: 0x%04X", (ACLPacketHeader->ConnectionHandle & 0x0FFF));
	BT_ACL_DEBUG("-- Data Length: 0x%04X", ACLPacketHeader->DataLength);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DataHeader->DestinationChannel);
	BT_ACL_DEBUG("-- Payload Length: 0x%04X", DataHeader->PayloadLength);			
	#endif
	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG(">> L2CAP Connection Response", NULL);
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- Source Channel: 0x%04X", ConnectionResponse.SourceChannel);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", ConnectionResponse.DestinationChannel);
	#endif
}

static inline void Bluetooth_Signal_ConfigurationReq(BT_ACL_Header_t* ACLPacketHeader,
                                                     BT_DataPacket_Header_t* DataHeader,
                                                     BT_Signal_Header_t* SignalCommandHeader)
{
	BT_Signal_ConfigurationReq_t ConfigurationRequest;
	Pipe_Read_Stream_LE(&ConfigurationRequest, sizeof(ConfigurationRequest));
	
	// TODO: Process/Discard configuration options here
	Pipe_Discard_Stream(DataHeader->PayloadLength - sizeof(*SignalCommandHeader));

	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG("<< L2CAP Configuration Request", NULL);
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", ConfigurationRequest.DestinationChannel);
	#endif
	
	Pipe_ClearIN();
	Pipe_Freeze();
	Pipe_SelectPipe(BLUETOOTH_DATA_OUT_PIPE);
	Pipe_Unfreeze();
	
	BT_Signal_ConfigurationResp_t ConfigurationResponse;

	ACLPacketHeader->DataLength           = sizeof(*DataHeader) + sizeof(*SignalCommandHeader) + sizeof(ConfigurationResponse);
	DataHeader->PayloadLength             = sizeof(*SignalCommandHeader) + sizeof(ConfigurationResponse);
	DataHeader->DestinationChannel        = BT_CHANNEL_SIGNALING;
	SignalCommandHeader->Code             = BT_SIGNAL_CONFIGURATION_RESPONSE;
	SignalCommandHeader->Length           = sizeof(ConfigurationResponse);
	
	Bluetooth_Channel_t* ChannelData      = Bluetooth_GetChannelData(ConfigurationRequest.DestinationChannel, false);

	if (ChannelData != NULL)
	{
		switch (ChannelData->State)
		{
			case Channel_Config_WaitConfig:
				ChannelData->State = Channel_Config_WaitSendConfig;
				break;
			case Channel_Config_WaitReqResp:
				ChannelData->State = Channel_Config_WaitResp;
				break;
			case Channel_Config_WaitReq:
				ChannelData->State = Channel_Open;
				break;
		}
	}
	  
	// TODO: Add channel config data to the tail of ConfigurationResponse

	ConfigurationResponse.SourceChannel   = ChannelData->RemoteNumber;
	ConfigurationResponse.Flags           = 0x00;
	ConfigurationResponse.Result          = (ChannelData != NULL) ? BT_CONFIGURATION_SUCCESSFUL : BT_CONFIGURATION_REJECTED;

	Pipe_Write_Stream_LE(ACLPacketHeader, sizeof(*ACLPacketHeader));
	Pipe_Write_Stream_LE(DataHeader, sizeof(*DataHeader));
	Pipe_Write_Stream_LE(SignalCommandHeader, sizeof(*SignalCommandHeader));
	Pipe_Write_Stream_LE(&ConfigurationResponse, sizeof(ConfigurationResponse));
	
	Pipe_ClearOUT();
	Pipe_Freeze();
	
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("Packet Sent", NULL);
	BT_ACL_DEBUG("-- Connection Handle: 0x%04X", (ACLPacketHeader->ConnectionHandle & 0x0FFF));
	BT_ACL_DEBUG("-- Data Length: 0x%04X", ACLPacketHeader->DataLength);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DataHeader->DestinationChannel);
	BT_ACL_DEBUG("-- Payload Length: 0x%04X", DataHeader->PayloadLength);			
	#endif
	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG(">> L2CAP Configuration Response", NULL);
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- Result: 0x%02X", ConfigurationResponse.Result);
	#endif
}

static inline void Bluetooth_Signal_DisconnectionReq(BT_ACL_Header_t* ACLPacketHeader,
                                                     BT_DataPacket_Header_t* DataHeader,
                                                     BT_Signal_Header_t* SignalCommandHeader)
{
	BT_Signal_DisconnectionReq_t DisconnectionRequest;
	
	Pipe_Read_Stream_LE(&DisconnectionRequest, sizeof(DisconnectionRequest));

	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG("<< L2CAP Disconnection Request", NULL);
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DisconnectionRequest.DestinationChannel);
	BT_ACL_DEBUG("-- Source Channel: 0x%04X", DisconnectionRequest.SourceChannel);
	#endif
	
	Pipe_ClearIN();
	Pipe_Freeze();
	Pipe_SelectPipe(BLUETOOTH_DATA_OUT_PIPE);
	Pipe_Unfreeze();
	
	BT_Signal_DisconnectionResp_t DisconnectionResponse;

	ACLPacketHeader->DataLength           = sizeof(*DataHeader) + sizeof(*SignalCommandHeader) + sizeof(DisconnectionResponse);
	DataHeader->PayloadLength             = sizeof(*SignalCommandHeader) + sizeof(DisconnectionResponse);
	DataHeader->DestinationChannel        = BT_CHANNEL_SIGNALING;
	SignalCommandHeader->Code             = BT_SIGNAL_DISCONNECTION_RESPONSE;
	SignalCommandHeader->Length           = sizeof(DisconnectionResponse);
	
	Bluetooth_Channel_t* ChannelData      = Bluetooth_GetChannelData(DisconnectionRequest.SourceChannel, true);

	if (ChannelData != NULL)
	  ChannelData->State = Channel_Closed;
	
	DisconnectionResponse.DestinationChannel = ChannelData->LocalNumber;
	DisconnectionResponse.SourceChannel      = ChannelData->RemoteNumber;

	Pipe_Write_Stream_LE(ACLPacketHeader, sizeof(*ACLPacketHeader));
	Pipe_Write_Stream_LE(DataHeader, sizeof(*DataHeader));
	Pipe_Write_Stream_LE(SignalCommandHeader, sizeof(*SignalCommandHeader));
	Pipe_Write_Stream_LE(&DisconnectionResponse, sizeof(DisconnectionResponse));
	
	Pipe_ClearOUT();
	Pipe_Freeze();
	
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("Packet Sent", NULL);
	BT_ACL_DEBUG("-- Connection Handle: 0x%04X", (ACLPacketHeader->ConnectionHandle & 0x0FFF));
	BT_ACL_DEBUG("-- Data Length: 0x%04X", ACLPacketHeader->DataLength);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DataHeader->DestinationChannel);
	BT_ACL_DEBUG("-- Payload Length: 0x%04X", DataHeader->PayloadLength);			
	#endif
	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG(">> L2CAP Disconnection Response", NULL);
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- Source Channel: 0x%04X", DisconnectionResponse.SourceChannel);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DisconnectionResponse.DestinationChannel);
	#endif
}

static inline void Bluetooth_Signal_EchoReq(BT_ACL_Header_t* ACLPacketHeader,
                                            BT_DataPacket_Header_t* DataHeader,
                                            BT_Signal_Header_t* SignalCommandHeader)
{
	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG("<< L2CAP Echo Request", NULL);
	#endif
	
	Pipe_ClearIN();
	Pipe_Freeze();
	Pipe_SelectPipe(BLUETOOTH_DATA_OUT_PIPE);
	Pipe_Unfreeze();
	
	ACLPacketHeader->DataLength           = sizeof(*DataHeader) + sizeof(*SignalCommandHeader);
	DataHeader->PayloadLength             = sizeof(*SignalCommandHeader);
	DataHeader->DestinationChannel        = BT_CHANNEL_SIGNALING;
	SignalCommandHeader->Code             = BT_SIGNAL_ECHO_RESPONSE;
	SignalCommandHeader->Length           = 0;
	
	Pipe_Write_Stream_LE(ACLPacketHeader, sizeof(*ACLPacketHeader));
	Pipe_Write_Stream_LE(DataHeader, sizeof(*DataHeader));
	Pipe_Write_Stream_LE(SignalCommandHeader, sizeof(*SignalCommandHeader));
	
	Pipe_ClearOUT();		
	Pipe_Freeze();
	
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("Packet Sent", NULL);
	BT_ACL_DEBUG("-- Connection Handle: 0x%04X", (ACLPacketHeader->ConnectionHandle & 0x0FFF));
	BT_ACL_DEBUG("-- Data Length: 0x%04X", ACLPacketHeader->DataLength);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DataHeader->DestinationChannel);
	BT_ACL_DEBUG("-- Payload Length: 0x%04X", DataHeader->PayloadLength);			
	#endif
	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG(">> L2CAP Echo Response", NULL);
	#endif
}

static inline void Bluetooth_Signal_InformationReq(BT_ACL_Header_t* ACLPacketHeader,
                                                   BT_DataPacket_Header_t* DataHeader,
                                                   BT_Signal_Header_t* SignalCommandHeader)
{
	BT_Signal_InformationReq_t InformationRequest;

	Pipe_Read_Stream_LE(&InformationRequest, sizeof(InformationRequest));

	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG("<< Information Request", NULL);
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- Info Type: 0x%04X", InformationRequest.InfoType);
	#endif
	
	Pipe_ClearIN();
	Pipe_Freeze();
	Pipe_SelectPipe(BLUETOOTH_DATA_OUT_PIPE);
	Pipe_Unfreeze();
	
	BT_Signal_InformationResp_t InformationResponse;
	uint8_t ResponseData[4];
	uint8_t ResponseLen;

	switch (InformationRequest.InfoType)
	{
		case BT_INFOREQ_MTU:		
			InformationResponse.Result = BT_INFORMATION_SUCCESSFUL;
			ResponseLen = 2;
			
			*((uint16_t*)&ResponseData) = 65533;
			break;
		case BT_INFOREQ_EXTENDEDFEATURES:
			InformationResponse.Result = BT_INFORMATION_SUCCESSFUL;
			ResponseLen = 4;
			
			*((uint32_t*)&ResponseData) = 0;
			break;
		default:
			InformationResponse.Result = BT_INFORMATION_NOTSUPPORTED;
			ResponseLen = 0;
			break;
	}
	
	ACLPacketHeader->DataLength           = sizeof(*DataHeader) + sizeof(*SignalCommandHeader) + sizeof(InformationResponse) +
	                                        ResponseLen;
	DataHeader->PayloadLength             = sizeof(*SignalCommandHeader) + sizeof(InformationResponse) + ResponseLen;
	DataHeader->DestinationChannel        = BT_CHANNEL_SIGNALING;
	SignalCommandHeader->Code             = BT_SIGNAL_INFORMATION_RESPONSE;
	SignalCommandHeader->Length           = sizeof(InformationResponse) + ResponseLen;
	
	Pipe_Write_Stream_LE(ACLPacketHeader, sizeof(*ACLPacketHeader));
	Pipe_Write_Stream_LE(DataHeader, sizeof(*DataHeader));
	Pipe_Write_Stream_LE(SignalCommandHeader, sizeof(*SignalCommandHeader));
	Pipe_Write_Stream_LE(&InformationResponse, sizeof(InformationResponse));
	Pipe_Write_Stream_LE(ResponseData, ResponseLen);
	
	Pipe_ClearOUT();		
	Pipe_Freeze();

	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("Packet Sent", NULL);
	BT_ACL_DEBUG("-- Connection Handle: 0x%04X", (ACLPacketHeader->ConnectionHandle & 0x0FFF));
	BT_ACL_DEBUG("-- Data Length: 0x%04X", ACLPacketHeader->DataLength);
	BT_ACL_DEBUG("-- Destination Channel: 0x%04X", DataHeader->DestinationChannel);
	BT_ACL_DEBUG("-- Payload Length: 0x%04X", DataHeader->PayloadLength);			
	#endif
	#if (ACL_DEBUG_LEVEL > 0)
	BT_ACL_DEBUG(">> L2CAP Information Response", NULL);	
	#endif
	#if (ACL_DEBUG_LEVEL > 1)
	BT_ACL_DEBUG("-- Result: 0x%02X", InformationResponse.Result);
	#endif
}
