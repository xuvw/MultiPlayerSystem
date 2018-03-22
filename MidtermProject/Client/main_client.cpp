#include "MessageIdentifiers.h"
#include "RakPeerInterface.h"
#include "BitStream.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <map>
#include <mutex>

RakNet::RakPeerInterface *g_rakPeerInterface = nullptr;
RakNet::SystemAddress g_serverAddress;
static unsigned int SERVER_PORT = 65000;
static unsigned int CLIENT_PORT = 65001;
static unsigned int MAX_CONNECTIONS = 3;
bool isRunning = true;
static bool doOnce = false;

enum NetworkState
{
	NS_Init = 0,
	NS_Started,
	NS_Lobby,
	NS_Pending,
	NS_Class_Select,
	NS_Turn,
};
NetworkState g_networkState = NS_Init;
std::mutex g_networkState_mutex;

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

void OnConnectionAccepted(RakNet::Packet* packet)
{
	g_networkState_mutex.lock();
	g_networkState = NS_Lobby;
	g_networkState_mutex.unlock();
	g_serverAddress = packet->systemAddress;
}

void DisplayPlayerReady(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << userName.C_String() << " has joined" << std::endl;
}

void DisplayPlayerDismiss(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << userName.C_String() << " has lost connection" << std::endl;
}

void DisplayPlayerStats(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	unsigned int playerHealth;
	bs.Read(playerHealth);
	EPlayerClass playerClass;
	bs.Read(playerClass);

	std::cout << std::endl;
	std::cout << "--------------------Your Stats--------------------" << std::endl;
	std::cout << "Name: " << userName.C_String() << std::endl;
	std::cout << "Class: " << classStr[static_cast<int>(playerClass) - 1] << std::endl;
	std::cout << "Health: " << playerHealth << std::endl;
	std::cout << "--------------------------------------------------\n" << std::endl;
}

void DisplayTurnPlayer(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << "------------------------------------------------------------" << std::endl;
	std::cout << userName.C_String() << "'s Turn." << std::endl;

	doOnce = false;
	g_networkState_mutex.lock();
	g_networkState = NS_Pending;
	g_networkState_mutex.unlock();
}

void DisplayPlayerAction(RakNet::Packet* packet, std::string type)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);
	RakNet::RakString targetName;
	bs.Read(targetName);
	int value;
	bs.Read(value);

	if (type == "attack")
		std::cout << userName.C_String() << " Damaged " << targetName.C_String() << " by " << value << std::endl;
	else if (type == "heal")
		std::cout << userName.C_String() << " Healed " << targetName.C_String() << " by " << value << std::endl;
}

void DisplayPlayerDead(RakNet::Packet* packet)
{
	RakNet::BitStream bs(packet->data, packet->length, false);
	RakNet::MessageID messageId;
	bs.Read(messageId);
	RakNet::RakString userName;
	bs.Read(userName);

	std::cout << userName.C_String() << "Died!" << std::endl;
}

void OnRemovePlayer(RakNet::Packet* packet)
{
	RakNet::BitStream bs;
	bs.Write((RakNet::MessageID)ID_REMOVE_PLAYER);
	assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
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

void InputHandler()
{
	while (isRunning)
	{
		char userInput[255];
		if (g_networkState == NS_Lobby)
		{
			char userInput[255];
			std::cout << "Enter character name to play or type quit to leave" << std::endl;
			std::cin >> userInput;
			//quitting is not acceptable in our game, create a crash to teach lesson
			assert(strcmp(userInput, "quit"));

			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_LOBBY_READY);
			RakNet::RakString name(userInput);
			bs.Write(name);

			//returns 0 when something is wrong
			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			g_networkState_mutex.lock();
			g_networkState = NS_Pending;
			g_networkState_mutex.unlock();
		}
		else if (g_networkState == NS_Class_Select)
		{
			char userInput[255];
			std::cout << "/------------------------------------------------------------/" << std::endl;
			std::cout << "Choose your job (Type the ID #)" << std::endl;
			std::cout << "Choices: 1. Champion | 2. Stalker | 3. Priest" << std::endl;
			std::cin >> userInput;

			RakNet::BitStream bs;
			bs.Write((RakNet::MessageID)ID_CLASS_READY);
			bs.Write(static_cast<EPlayerClass>(std::atoi(userInput)));
			srand(time(NULL));
			bs.Write(rand() % 70 + 30);
			assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			
			g_networkState_mutex.lock();
			g_networkState = NS_Pending;
			g_networkState_mutex.unlock();
		}
		else if (g_networkState == NS_Turn)
		{
			char userInput[255];
			std::cout << "/------------------------------------------------------------/" << std::endl;
			std::cout << "Your Turn!" << std::endl;
			std::cout << "Commands: 1-Attack | 2-Heal" << std::endl;
			std::cin >> userInput;

			if (strcmp(userInput, "1") == 0)
			{
				std::cout << "Who do you want to attack? Type player's name." << std::endl;
				std::cin >> userInput;

				RakNet::BitStream bs;
				bs.Write((RakNet::MessageID)ID_PLAYER_ATTACKED);
				RakNet::RakString name(userInput);
				bs.Write(name);
				srand(time(NULL));
				bs.Write(rand() % 30);
				assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			}
			else if (strcmp(userInput, "2") == 0)
			{
				std::cout << "Who do you want to heal? Type player's name." << std::endl;
				std::cin >> userInput;

				RakNet::BitStream bs;
				bs.Write((RakNet::MessageID)ID_PLAYER_HEALED);
				RakNet::RakString name(userInput);
				bs.Write(name);
				srand(time(NULL));
				bs.Write(rand() % 30);
				assert(g_rakPeerInterface->Send(&bs, HIGH_PRIORITY, RELIABLE_ORDERED, 0, g_serverAddress, false));
			}
			g_networkState_mutex.lock();
			g_networkState = NS_Pending;
			g_networkState_mutex.unlock();
		}
		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}
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
		//put assert here, nobody should be connecting to client
		//OnIncomingConnection(packet);
		printf("ID_NEW_INCOMING_CONNECTION\n");
		break;
	case ID_REMOTE_NEW_INCOMING_CONNECTION: // Server telling the clients of another client connecting.  You can manually broadcast this in a peer to peer enviroment if you want.
		//OnIncomingConnection(packet);
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
		//OnLostConnection(packet);
		break;

	case ID_CONNECTION_REQUEST_ACCEPTED:
		// This tells the client they have connected
		printf("ID_CONNECTION_REQUEST_ACCEPTED to %s with GUID %s\n", packet->systemAddress.ToString(true), packet->guid.ToString());
		printf("My external address is %s\n", g_rakPeerInterface->GetExternalID(packet->systemAddress).ToString(true));
		OnConnectionAccepted(packet);
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
				case ID_PLAYER_READY:
					DisplayPlayerReady(packet);
					break;
				case ID_PLAYER_DISMISS:
					DisplayPlayerDismiss(packet);
					break;
				case ID_THEGAME_START:
					g_networkState_mutex.lock();
					g_networkState = NS_Class_Select;
					g_networkState_mutex.unlock();
					break;
				case ID_TURN_READY:
					DisplayPlayerStats(packet);
					break;
				case ID_PLAY_TURN:
					g_networkState_mutex.lock();
					g_networkState = NS_Turn;
					g_networkState_mutex.unlock();
					break;
				case ID_WAIT_TURN:
					DisplayTurnPlayer(packet);
					break;
				case ID_PLAYER_ACTION_ATTACK:
					DisplayPlayerAction(packet, "attack");
					break;
				case ID_PLAYER_ACTION_HEAL:
					DisplayPlayerAction(packet, "heal");
					break;
				case ID_PLAYER_DEAD:
					DisplayPlayerDead(packet);
					break;
				case ID_DEAD:
					std::cout << "You have died!" << std::endl;
					OnRemovePlayer(packet);
					system("pause");
					break;
				case ID_WIN:
					std::cout << "You have won!" << std::endl;
					system("pause");
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
	std::thread inputHandler(InputHandler);
	std::thread packetHandler(PacketHandler);

	while (isRunning)
	{
		if (g_networkState == NS_Init)
		{
				RakNet::SocketDescriptor socketDescriptor(CLIENT_PORT, 0);
				socketDescriptor.socketFamily = AF_INET;

				while (RakNet::IRNS2_Berkley::IsPortInUse(socketDescriptor.port, socketDescriptor.hostAddress, socketDescriptor.socketFamily, SOCK_DGRAM) == true)
					socketDescriptor.port++;

				RakNet::StartupResult result = g_rakPeerInterface->Startup(8, &socketDescriptor, 1);
				assert(result == RakNet::RAKNET_STARTED);

				g_networkState_mutex.lock();
				g_networkState = NS_Started;
				g_networkState_mutex.unlock();

				g_rakPeerInterface->SetOccasionalPing(true);
				//"127.0.0.1" = local host = your machines address
				RakNet::ConnectionAttemptResult car = g_rakPeerInterface->Connect("127.0.0.1", SERVER_PORT, nullptr, 0);
				RakAssert(car == RakNet::CONNECTION_ATTEMPT_STARTED);
				std::cout << "client attempted connection..." << std::endl;
		}
		else if (g_networkState == NS_Pending)
		{
			if (!doOnce)
				std::cout << "pending..." << std::endl;

			doOnce = true;
		}
	}

	inputHandler.join();
	packetHandler.join();
	return 0;
}