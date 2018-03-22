#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include "BitStream.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>

RakNet::RakPeerInterface *g_rakPeerInterface = nullptr;
static unsigned int SERVER_PORT = 65000;
static unsigned int MAX_CONNECTIONS = 3;
bool isRunning = true;


int playerCount = 0;
int turn = 0;

enum ServerState
{
	ID_LOBBY_READY = ID_USER_PACKET_ENUM,
	ID_CLASS_READY,
	ID_PLAYER_ATTACKED,
	ID_PLAYER_HEALED,
	ID_REMOVE_PLAYER
};

enum ClientState
{
	ID_PLAYER_READY = ID_USER_PACKET_ENUM,
	ID_PLAYER_DISMISS,
	ID_THEGAME_START,
	ID_TURN_READY,
	ID_PLAY_TURN,
	ID_WAIT_TURN,
	ID_PLAYER_ACTION_ATTACK,
	ID_PLAYER_ACTION_HEAL,
	ID_PLAYER_DEAD,
	ID_DEAD,
	ID_WIN
};

enum EPlayerClass
{
	Champion = 1,
	Stalker,
	Priest,
};
const char* classStr[] = { "Champion", "Stalker", "Priest" };

struct SPlayer
{
	std::string m_name = "";
	unsigned int m_health = 0;
	EPlayerClass m_class;

	//function to send a packet with name/health/class etc
	void Send(RakNet::SystemAddress systemAddress, bool isBroadcast, ClientState gameState)
	{
		RakNet::RakString name(m_name.c_str());

		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)gameState);
		writeBs.Write(name);
		writeBs.Write(m_health);
		writeBs.Write(m_class);

		//returns 0 when something is wrong
		assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, systemAddress, isBroadcast));
	}
};
std::map<RakNet::RakNetGUID, SPlayer> m_players;

SPlayer& GetPlayer(RakNet::RakNetGUID raknetId)
{
	std::map<RakNet::RakNetGUID, SPlayer>::iterator it = m_players.find(raknetId);
	assert(it != m_players.end());
	return it->second;
}

void ChangeTurn()
{
	for (std::map<RakNet::RakNetGUID, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		SPlayer& p = it->second;
		p.Send(g_rakPeerInterface->GetSystemAddressFromGuid(it->first), false, ID_TURN_READY);
	}

	int c = 1;
	for (std::map<RakNet::RakNetGUID, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it, ++c)
	{
		SPlayer& p = it->second;
		if (c == turn)
		{
			RakNet::SystemAddress turnAddress = g_rakPeerInterface->GetSystemAddressFromGuid(it->first);
			p.Send(turnAddress, false, ID_PLAY_TURN);

			RakNet::BitStream writeBs;
			writeBs.Write((RakNet::MessageID)ID_WAIT_TURN);
			RakNet::RakString name(p.m_name.c_str());
			writeBs.Write(name);
			assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, turnAddress, true));
		}
	}
	turn++;
	if (turn > m_players.size()) turn = 1;
}

void OnLostConnection(RakNet::Packet* packet)
{
	SPlayer& lostPlayer = GetPlayer(packet->guid);
	lostPlayer.Send(RakNet::UNASSIGNED_SYSTEM_ADDRESS, true, ID_PLAYER_DISMISS);
	m_players.erase(packet->guid);
}

void OnIncomingConnection(RakNet::Packet* packet)
{
	m_players.insert(std::make_pair(packet->guid, SPlayer()));
	std::cout << "Number of Players in game: " << m_players.size() << std::endl;
}

void OnLobbyReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	SPlayer& player = GetPlayer(packet->guid);
	player.m_name = userName;
	std::cout << player.m_name.c_str() << " IS LOCKED IN!!!!!" << std::endl;

	player.Send(packet->systemAddress, true, ID_PLAYER_READY);

	playerCount++;
	if (playerCount == MAX_CONNECTIONS)
	{
		playerCount = 0;
		std::cout << "Locked In! Let the party starts" << std::endl;
		std::cout << "/------------------------------------------------------------/" << std::endl;
		player.Send(RakNet::UNASSIGNED_SYSTEM_ADDRESS, true, ID_THEGAME_START);
	}
}

void OnClassReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	EPlayerClass userClass;
	bs.Read(userClass);
	unsigned int userHealth;
	bs.Read(userHealth);

	SPlayer& player = GetPlayer(packet->guid);
	player.m_class = userClass;
	player.m_health = userHealth;
	std::cout << player.m_name.c_str() << " chose " << classStr[static_cast<int>(userClass) - 1] << std::endl;
	
	playerCount++;
	if (playerCount == MAX_CONNECTIONS)
	{
		playerCount = 0;
		std::cout << "Everyone chose their job!" << std::endl;
		std::cout << "/------------------------------------------------------------/" << std::endl;
		
		ChangeTurn();
	}
}

void OnPlayerAttack(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	int value;
	bs.Read(value);

	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_PLAYER_ACTION_ATTACK);
	writeBs.Write(GetPlayer(packet->guid).m_name.c_str());
	writeBs.Write(userName);
	writeBs.Write(value);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));

	for (std::map<RakNet::RakNetGUID, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		SPlayer& p = it->second;
		
		if (strcmp(p.m_name.c_str(), userName.C_String()) == 0)
		{
			if (p.m_health > value)
			{
				p.m_health -= value;
				ChangeTurn();
			}
			else
			{
				RakNet::BitStream writeBs;
				writeBs.Write((RakNet::MessageID)ID_PLAYER_DEAD);
				writeBs.Write(p.m_name);
				assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_rakPeerInterface->GetSystemAddressFromGuid(it->first), true));

				RakNet::BitStream writeBs2;
				writeBs2.Write((RakNet::MessageID)ID_DEAD);
				assert(g_rakPeerInterface->Send(&writeBs2, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_rakPeerInterface->GetSystemAddressFromGuid(it->first), false));
			}
		}
	}	
}

void OnPlayerHeal(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	int value;
	bs.Read(value);

	SPlayer& player = GetPlayer(packet->guid);
	RakNet::BitStream writeBs;
	writeBs.Write((RakNet::MessageID)ID_PLAYER_ACTION_HEAL);
	writeBs.Write(player.m_name.c_str());
	writeBs.Write(userName);
	writeBs.Write(value);
	assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, RakNet::UNASSIGNED_SYSTEM_ADDRESS, true));

	for (std::map<RakNet::RakNetGUID, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
	{
		SPlayer& p = it->second;
		if (strcmp(p.m_name.c_str(), userName.C_String()) == 0) p.m_health += value;
	}
	
	ChangeTurn();
}

void OnRemovePlayer(RakNet::Packet* packet)
{
	m_players.erase(packet->guid);

	if (m_players.size() == 1)
	{
		std::cout << "The Game has Ended!" << std::endl;
		std::cout << "/------------------------------------------------------------/" << std::endl;
		RakNet::BitStream writeBs;
		writeBs.Write((RakNet::MessageID)ID_WIN);
		for (std::map<RakNet::RakNetGUID, SPlayer>::iterator it = m_players.begin(); it != m_players.end(); ++it)
		{
			assert(g_rakPeerInterface->Send(&writeBs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_rakPeerInterface->GetSystemAddressFromGuid(it->first), false));
		}

		system("pause");
	}
	else
		ChangeTurn();
}

unsigned char GetPacketIdentifier(RakNet::Packet *packet)
{
	if (packet == nullptr)
		return 255;

	if ((unsigned char)packet->data[0] == ID_TIMESTAMP)
	{
		RakAssert(packet->length > sizeof(RakNet::MessageID) + sizeof(RakNet::Time));
		return (unsigned char)packet->data[sizeof(RakNet::MessageID) + sizeof(RakNet::Time)];
	}
	else
		return (unsigned char)packet->data[0];
}

bool HandleLowLevelPackets(RakNet::Packet* packet)
{
	bool isHandled = true;
	// We got a packet, get the identifier with our handy function
	unsigned char packetIdentifier = GetPacketIdentifier(packet);

	// Check if this is a network message packet
	switch (packetIdentifier)
	{
	case ID_DISCONNECTION_NOTIFICATION:
		// Connection lost normally
		printf("ID_DISCONNECTION_NOTIFICATION\n");
		break;
	case ID_ALREADY_CONNECTED:
		// Connection lost normally
		printf("ID_ALREADY_CONNECTED with guid %" PRINTF_64_BIT_MODIFIER "u\n", packet->guid);
		break;
	case ID_INCOMPATIBLE_PROTOCOL_VERSION:
		printf("ID_INCOMPATIBLE_PROTOCOL_VERSION\n");
		break;
	case ID_REMOTE_DISCONNECTION_NOTIFICATION: // Server telling the clients of another client disconnecting gracefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_DISCONNECTION_NOTIFICATION\n");
		break;
	case ID_REMOTE_CONNECTION_LOST: // Server telling the clients of another client disconnecting forcefully.  You can manually broadcast this in a peer to peer enviroment if you want.
		printf("ID_REMOTE_CONNECTION_LOST\n");
		break;
	case ID_NEW_INCOMING_CONNECTION:
		//client connecting to server
		OnIncomingConnection(packet);
		printf("ID_NEW_INCOMING_CONNECTION\n");
		break;
	case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
		OnIncomingConnection(packet);
		printf("ID_REMOTE_NEW_INCOMING_CONNECTION\n");
		break;
	case ID_CONNECTION_BANNED: // Banned from this server
		printf("We are banned from this server.\n");
		break;
	case ID_CONNECTION_ATTEMPT_FAILED:
		printf("Connection attempt failed\n");
		break;
	case ID_NO_FREE_INCOMING_CONNECTIONS:
		// Sorry, the server is full.  I don't do anything here but
		// A real app should tell the user
		printf("ID_NO_FREE_INCOMING_CONNECTIONS\n");
		break;

	case ID_INVALID_PASSWORD:
		printf("ID_INVALID_PASSWORD\n");
		break;

	case ID_CONNECTION_LOST:
		// Couldn't deliver a reliable packet - i.e. the other system was abnormally
		// terminated
		printf("ID_CONNECTION_LOST\n");
		OnLostConnection(packet);
		break;

	
	case ID_CONNECTED_PING:
	case ID_UNCONNECTED_PING:
		printf("Ping from %s\n", packet->systemAddress.ToString(true));
		break;
	default:
		isHandled = false;
		break;
	}
	return isHandled;
}

void PacketHandler()
{
	while (isRunning)
	{
		for (RakNet::Packet* packet = g_rakPeerInterface->Receive(); packet != nullptr; g_rakPeerInterface->DeallocatePacket(packet), packet = g_rakPeerInterface->Receive())
		{
			if (!HandleLowLevelPackets(packet))
			{
				//our game specific packets
				unsigned char packetIdentifier = GetPacketIdentifier(packet);
				switch (packetIdentifier)
				{
				case ID_LOBBY_READY:
					OnLobbyReady(packet);
					break;
				case ID_CLASS_READY:
					OnClassReady(packet);
					break;
				case ID_PLAYER_ATTACKED:
					OnPlayerAttack(packet);
					break;
				case ID_PLAYER_HEALED:
					OnPlayerHeal(packet);
					break;
				case ID_REMOVE_PLAYER:
					OnRemovePlayer(packet);
					break;
				default:
					break;
				}
			}
		}

		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}
}

int main()
{
	g_rakPeerInterface = RakNet::RakPeerInterface::GetInstance();
	std::thread packetHandler(PacketHandler);

	RakNet::SocketDescriptor socketDescriptors[1];
	socketDescriptors[0].port = SERVER_PORT;
	socketDescriptors[0].socketFamily = AF_INET; // Test out IPV4

	bool isSuccess = g_rakPeerInterface->Startup(MAX_CONNECTIONS, socketDescriptors, 1) == RakNet::RAKNET_STARTED;
	assert(isSuccess);

	g_rakPeerInterface->SetMaximumIncomingConnections(MAX_CONNECTIONS);
	std::cout << "Server started" << std::endl;

	srand(time(NULL));
	turn = rand() % 3 + 1;

	while (isRunning)
	{

	}

	packetHandler.join();
	return 0;
}