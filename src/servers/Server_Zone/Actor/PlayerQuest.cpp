#include <src/servers/Server_Common/Common.h>
#include <src/servers/Server_Common/Database/Database.h>
#include <src/servers/Server_Common/Network/PacketDef/Zone/ServerZoneDef.h>
#include <src/servers/Server_Common/Network/GamePacket.h>
#include <src/servers/Server_Common/Exd/ExdData.h>
#include <src/servers/Server_Common/Network/PacketContainer.h>

#include "src/servers/Server_Zone/Network/GameConnection.h"

#include "src/servers/Server_Zone/Network/PacketWrappers/QuestMessagePacket.h"

#include "src/servers/Server_Zone/Session.h"
#include "Player.h"
#include "src/servers/Server_Zone/Inventory/Inventory.h"



extern Core::Db::Database g_database;
extern Core::Data::ExdData g_exdData;

using namespace Core::Common;
using namespace Core::Network::Packets;
using namespace Core::Network::Packets::Server;

bool Core::Entity::Player::loadActiveQuests()
{

   auto pQR = g_database.query( "SELECT * FROM charaquest WHERE CharacterId = " + std::to_string( m_id ) + ";" );

   if( !pQR )
      return false;

   Db::Field* field = pQR->fetch();

   for( uint8_t i = 0; i < 30; i++ )
   {

      uint16_t index = i * 10;

      //g_log.debug( " QUEST_ID: " + std::to_string( field[index].getInt16() ) + " INDEX: " + std::to_string( index ) );

      if( field[index].get< int16_t >() != 0 )
      {
         
         boost::shared_ptr<QuestActive> pActiveQuest( new QuestActive() );
         pActiveQuest->c.questId = field[index].get< int16_t >();
         pActiveQuest->c.sequence = field[index + 1].get< uint8_t >();
         pActiveQuest->c.flags = field[index + 2].get< uint8_t >();
         pActiveQuest->c.UI8A = field[index + 3].get< uint8_t >();
         pActiveQuest->c.UI8B = field[index + 4].get< uint8_t >();
         pActiveQuest->c.UI8C = field[index + 5].get< uint8_t >();
         pActiveQuest->c.UI8D = field[index + 6].get< uint8_t >();
         pActiveQuest->c.UI8E = field[index + 7].get< uint8_t >();
         pActiveQuest->c.UI8F = field[index + 8].get< uint8_t >();
         pActiveQuest->c.padding1 = field[index + 9].get< uint8_t >();
         m_activeQuests[i] = pActiveQuest;

         m_questIdToQuestIdx[pActiveQuest->c.questId] = i;
         m_questIdxToQuestId[i] = pActiveQuest->c.questId;

      }
      else
      {
         m_activeQuests[i] = nullptr;
         m_freeQuestIdxQueue.push( i );
      }

   }

   return true;

}

void Core::Entity::Player::finishQuest( uint16_t questId )
{

   int16_t idx = getQuestIndex( questId );

   if( ( idx != -1 ) && ( m_activeQuests[idx] != nullptr ) )
   {
      GamePacketNew< FFXIVIpcQuestUpdate, ServerZoneIpcType > questUpdatePacket( getId() );
      questUpdatePacket.data().slot = idx;
      questUpdatePacket.data().questInfo.c.questId = 0;
      questUpdatePacket.data().questInfo.c.sequence = 0xFF;
      queuePacket( questUpdatePacket );

      GamePacketNew< FFXIVIpcQuestFinish, ServerZoneIpcType > questFinishPacket( getId() );
      questFinishPacket.data().questId = questId;
      questFinishPacket.data().flag1 = 1;
      questFinishPacket.data().flag2 = 1;
      queuePacket( questFinishPacket );

      updateQuestsCompleted( questId );
      setSyncFlag( PlayerSyncFlags::Quests );
      setSyncFlag( PlayerSyncFlags::QuestTracker );

      for( int32_t ii = 0; ii < 5; ii++ )
      {
         if( m_questTracking[ii] == idx )
            m_questTracking[ii] = -1;
      }

      boost::shared_ptr< QuestActive > pQuest = m_activeQuests[idx];
      m_activeQuests[idx].reset();

      m_freeQuestIdxQueue.push( idx );
      m_questIdToQuestIdx.erase( questId );
      m_questIdxToQuestId.erase( idx );
   }


   sendQuestTracker();

}

void Core::Entity::Player::unfinishQuest( uint16_t questId )
{
   removeQuestsCompleted( questId );
   setSyncFlag( PlayerSyncFlags::Quests );
   sendQuestInfo();
}

void Core::Entity::Player::removeQuest( uint16_t questId )
{

   int16_t idx = getQuestIndex( questId );

   if( ( idx != -1 ) && ( m_activeQuests[idx] != nullptr ) )
   {

      GamePacketNew< FFXIVIpcQuestUpdate, ServerZoneIpcType > questUpdatePacket( getId() );
      questUpdatePacket.data().slot = idx;
      questUpdatePacket.data().questInfo.c.questId = 0;
      questUpdatePacket.data().questInfo.c.sequence = 0xFF;
      queuePacket( questUpdatePacket );

      GamePacketNew< FFXIVIpcQuestFinish, ServerZoneIpcType > questFinishPacket( getId() );
      questFinishPacket.data().questId = questId;
      questFinishPacket.data().flag1 = 1;
      questFinishPacket.data().flag2 = 1;
      queuePacket( questFinishPacket );

      setSyncFlag( PlayerSyncFlags::Quests );
      setSyncFlag( PlayerSyncFlags::QuestTracker );

      for( int32_t ii = 0; ii < 5; ii++ )
      {
         if( m_questTracking[ii] == idx )
            m_questTracking[ii] = -1;
      }

      boost::shared_ptr<QuestActive> pQuest = m_activeQuests[idx];
      m_activeQuests[idx].reset();

      m_freeQuestIdxQueue.push( idx );
      m_questIdToQuestIdx.erase( questId );
      m_questIdxToQuestId.erase( idx );
   }

   sendQuestTracker();

}

bool Core::Entity::Player::hasQuest( uint16_t questId )
{
   return ( getQuestIndex( questId ) > -1 );
}

int16_t Core::Entity::Player::getQuestIndex( uint16_t questId )
{
   auto it = m_questIdToQuestIdx.find( questId );
   if( it != m_questIdToQuestIdx.end() )
      return it->second;

   return -1;
}

uint8_t Core::Entity::Player::getQuestBitFlag8( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->a.BitFlag8;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestBitFlag16( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->a.BitFlag16;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestBitFlag24( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->a.BitFlag24;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestBitFlag32( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->a.BitFlag32;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestBitFlag40( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->a.BitFlag40;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestBitFlag48( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->a.BitFlag48;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8A( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->c.UI8A;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8B( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->c.UI8B;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8C( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->c.UI8C;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8D( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->c.UI8D;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8E( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->c.UI8E;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8F( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->c.UI8F;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8AH( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8AH;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8BH( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8BH;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8CH( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8CH;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8DH( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8DH;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8EH( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8EH;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8FH( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8FH;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8AL( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8AL;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8BL( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8BL;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8CL( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8CL;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8DL( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8DL;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8EL( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8EL;
   }

   return value;
}

uint8_t Core::Entity::Player::getQuestUI8FL( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint8_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      value = pNewQuest->b.UI8FL;
   }

   return value;
}

uint16_t Core::Entity::Player::getQuestUI16A( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint16_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
   //   value = pNewQuest->d.UI16A;
   }

   return value;
}

uint16_t Core::Entity::Player::getQuestUI16B( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint16_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
   //   value = pNewQuest->d.UI16B;
   }

   return value;
}

uint16_t Core::Entity::Player::getQuestUI16C( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint16_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
    //  value = pNewQuest->d.UI16C;
   }

   return value;
}

uint32_t Core::Entity::Player::getQuestUI32A( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );
   uint32_t value = 0;
   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
     // value = pNewQuest->e.UI32A;
   }

   return value;
}

void Core::Entity::Player::setQuestUI8A( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->c.UI8A = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8B( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->c.UI8B = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8C( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->c.UI8C = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8D( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->c.UI8D = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8E( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->c.UI8E = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8F( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->c.UI8F = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8AH( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8AH = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8BH( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8BH = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8CH( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8CH = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8DH( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8DH = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8EH( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8EH = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8FH( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8FH = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8AL( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8AL = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8BL( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8BL = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8CL( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8CL = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8DL( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8DL = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8EL( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8EL = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI8FL( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->b.UI8FL = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI16A( uint16_t questId, uint16_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

   //   pNewQuest->d.UI16A = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI16B( uint16_t questId, uint16_t val )

{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

    //  pNewQuest->d.UI16B = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI16C( uint16_t questId, uint16_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

//      pNewQuest->d.UI16C = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestUI32A( uint16_t questId, uint32_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

     // pNewQuest->e.UI32A = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestBitFlag8( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->a.BitFlag8 = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestBitFlag16( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->a.BitFlag16 = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestBitFlag24( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->a.BitFlag24 = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}
void Core::Entity::Player::setQuestBitFlag32( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->a.BitFlag32 = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestBitFlag40( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->a.BitFlag40 = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}

void Core::Entity::Player::setQuestBitFlag48( uint16_t questId, uint8_t val )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];

      pNewQuest->a.BitFlag48 = val;

      updateQuest( questId, pNewQuest->c.sequence );
      setSyncFlag( PlayerSyncFlags::Quests );
   }
}



uint8_t Core::Entity::Player::getQuestSeq( uint16_t questId )
{
   int16_t idx = getQuestIndex( questId );

   if( idx != -1 )
   {
      boost::shared_ptr< QuestActive > pNewQuest = m_activeQuests[idx];
      return pNewQuest->c.sequence;
   }
   return 0;
}

void Core::Entity::Player::updateQuest( uint16_t questId, uint8_t sequence )
{
   if( hasQuest( questId ) )
   {
      GamePacketNew< FFXIVIpcQuestUpdate, ServerZoneIpcType > pe_qa( getId() );
      int16_t index = getQuestIndex( questId );
      auto pNewQuest = m_activeQuests[index];
      pe_qa.data().slot = index;
      pNewQuest->c.sequence = sequence;
      pe_qa.data().questInfo = *pNewQuest;
      queuePacket( pe_qa );

      setSyncFlag( PlayerSyncFlags::Quests );

   }
   else
   {

      int8_t idx = m_freeQuestIdxQueue.front();
      m_freeQuestIdxQueue.pop();

      GamePacketNew< FFXIVIpcQuestUpdate, ServerZoneIpcType > pe_qa( getId() );

      boost::shared_ptr< QuestActive > pNewQuest( new QuestActive() );
      pNewQuest->c.questId = questId;
      pNewQuest->c.sequence = sequence;
      pNewQuest->c.padding = 0;
      m_activeQuests[idx] = pNewQuest;

      m_questIdToQuestIdx[questId] = idx;
      m_questIdxToQuestId[idx] = questId;

      pe_qa.data().slot = idx;
      pNewQuest->c.sequence = sequence;
      pe_qa.data().questInfo = *pNewQuest;


      queuePacket( pe_qa );

      setSyncFlag( PlayerSyncFlags::Quests );
      setSyncFlag( PlayerSyncFlags::QuestTracker );

      for( int32_t ii = 0; ii < 5; ii++ )
      {
         if( m_questTracking[ii] == -1 )
         {
            m_questTracking[ii] = idx;
            break;
         }
      }

      sendQuestTracker();

   }
}

void Core::Entity::Player::sendQuestTracker()
{
   GamePacketNew< FFXIVIpcQuestTracker, ServerZoneIpcType > trackerPacket( getId() );

   for( int32_t ii = 0; ii < 5; ii++ )
   {
      if( m_questTracking[ii] >= 0 )
      {
         trackerPacket.data().entry[ii].active = 1;
         trackerPacket.data().entry[ii].questIndex = m_questTracking[ii];
      }
   }
   queuePacket( trackerPacket );
}

void Core::Entity::Player::setQuestTracker( uint16_t index, int16_t flag )
{
   if( flag == 0 )
   {
      //remove
      for( uint8_t ii = 0; ii < 5; ii++ )
      {
         if( m_questTracking[ii] == index )
         {
            m_questTracking[ii] = -1;
            break;
         }
      }
   }
   else
   {
      //add
      for( uint8_t ii = 0; ii < 5; ii++ )
      {
         if( m_questTracking[ii] == -1 )
         {
            m_questTracking[ii] = index;
            break;
         }
      }
   }

   setSyncFlag( PlayerSyncFlags::QuestTracker );

}


void Core::Entity::Player::sendQuestInfo()
{
   GamePacketNew< FFXIVIpcQuestActiveList, ServerZoneIpcType > pe_qa( getId() );

   for( int32_t i = 0; i < 30; i++ )
   {
      uint8_t offset = i * 12;
      if( m_activeQuests[i] != nullptr )
      {

         auto& quest = pe_qa.data().activeQuests[i];
         quest = *m_activeQuests[i];

      }
   }

   queuePacket( pe_qa );

   GamePacketNew< FFXIVIpcQuestCompleteList, ServerZoneIpcType > pe_qc( getId() );
   memcpy( pe_qc.data().questCompleteMask, m_questCompleteFlags, 200 );
   queuePacket( pe_qc );

   sendQuestTracker();
}

void Core::Entity::Player::sendQuestMessage( uint32_t questId, int8_t msgId, uint8_t type, uint32_t var1, uint32_t var2 )
{
   queuePacket( QuestMessagePacket( getAsPlayer(), questId, msgId, type, var1, var2 ) );
}


void Core::Entity::Player::updateQuestsCompleted( uint32_t questId )
{
   uint8_t index = questId / 8;
   uint8_t bitIndex = ( questId ) % 8;

   uint8_t value = 0x80 >> bitIndex;

   m_questCompleteFlags[index] |= value;

   setSyncFlag( PlayerSyncFlags::Quests );
}

void Core::Entity::Player::removeQuestsCompleted( uint32_t questId )
{
   uint8_t index = questId / 8;
   uint8_t bitIndex = ( questId ) % 8;

   uint8_t value = 0x80 >> bitIndex;

   m_questCompleteFlags[index] ^= value;

   setSyncFlag( PlayerSyncFlags::Quests );
}

bool Core::Entity::Player::giveQuestRewards( uint32_t questId, uint32_t optionalChoice )
{
   uint32_t playerLevel = getLevel();
   auto questInfo = g_exdData.getQuestInfo( questId );
   

   if( !questInfo )
      return false;

   auto paramGrowth = g_exdData.m_paramGrowthInfoMap[questInfo->quest_level];

   uint32_t exp = ( questInfo->reward_exp_factor * paramGrowth.quest_exp_mod * ( 45 + 5 * questInfo->quest_level) ) / 100;
   exp = exp + ( questInfo->reward_exp_factor / 100 ) * 10000;

   exp = questInfo->reward_exp_factor;

   uint16_t rewardItemCount = questInfo->reward_item.size();
   uint16_t optionalItemCount = questInfo->reward_item_optional.size() > 0 ? 1 : 0;

   uint32_t gilReward = questInfo->reward_gil;

   // TODO: check if there is room in inventory, else return false;
   if( exp > 0 )
      gainExp( exp );

   if( rewardItemCount > 0 )
   {
      for( uint32_t i = 0; i < questInfo->reward_item.size(); i++ )
      {
         // TODO: add the correct amount of items instead of 1
         addItem( -1, questInfo->reward_item.at( i ), questInfo->reward_item_count.at( i ) );
      }
   }

   if( optionalItemCount > 0 )
   {
      auto itemId = questInfo->reward_item_optional.at( optionalChoice );
      // TODO: add the correct amount of items instead of 1
      addItem( -1, itemId, questInfo->reward_item_optional_count.at( optionalChoice ) );
   }
   
   if( gilReward > 0 )
      addCurrency( 1, gilReward );

   return true;
}

boost::shared_ptr<QuestActive> Core::Entity::Player::getQuestActive( uint16_t index )
{
   return m_activeQuests[index];
}

