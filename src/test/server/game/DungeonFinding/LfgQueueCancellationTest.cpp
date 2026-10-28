/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "LFGGroupData.h"
#include "LFGMgr.h"
#include "LFGPlayerData.h"
#include "WorldMock.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>

namespace lfg
{
class LfgQueueCancellationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _previousWorld = std::move(sWorld);
        auto* world = new ::testing::NiceMock<WorldMock>();
        ON_CALL(*world, getIntConfig(::testing::_)).WillByDefault(::testing::Return(5));
        sWorld.reset(world);
        _manager = new LFGMgr();
    }

    void TearDown() override
    {
        delete _manager;
        sWorld = std::move(_previousWorld);
    }

    void PrepareGroup(bool continuing)
    {
        auto& group = _manager->GroupsStore[_group];
        group.SetLeader(_players.front());
        group.SetDungeon(12);
        if (continuing)
        {
            group.SetState(LFG_STATE_DUNGEON);
            group.DecreaseKicksLeft();
        }

        for (std::size_t i = 0; i < _players.size(); ++i)
        {
            ObjectGuid guid = _players[i];
            group.AddPlayer(guid);
            auto& player = _manager->PlayersStore[guid];
            player.SetGroup(_group);
            player.SetTeam(TEAM_ALLIANCE);
            player.SetSelectedDungeons(LfgDungeonSet{ i == 0 ? 258u : 12u });
            player.SetRoles(PLAYER_ROLE_DAMAGE);
            player.SetComment("Queued for a dungeon");
            player.SetRandomPlayersCount(3);
            if (continuing)
                player.SetState(LFG_STATE_DUNGEON);
        }

        QueueGroup();
    }

    void QueueGroup()
    {
        _manager->GroupsStore[_group].SetState(LFG_STATE_ROLECHECK);
        _manager->GroupsStore[_group].SetState(LFG_STATE_QUEUED);
        LfgRolesMap roles;
        for (ObjectGuid guid : _players)
        {
            _manager->PlayersStore[guid].SetState(LFG_STATE_ROLECHECK);
            _manager->PlayersStore[guid].SetState(LFG_STATE_QUEUED);
            roles[guid] = PLAYER_ROLE_DAMAGE;
        }
        _manager->GetQueue(_group).AddQueueData(_group, 123, LfgDungeonSet{ 12 }, roles);
        EXPECT_EQ(_manager->GetQueue(_group).QueueDataStore.count(_group), 1u);
    }

    void CancelGroupQueue()
    {
        // The client opcode handler invokes both methods in this order.
        _manager->LeaveLfg(_group);
        _manager->LeaveAllLfgQueues(_players.front(), true, _group);
        EXPECT_EQ(_manager->GetQueue(_group).QueueDataStore.count(_group), 0u);
    }

    void PrepareAndCancelSoloQueue()
    {
        ObjectGuid guid = _players.front();
        auto& player = _manager->PlayersStore[guid];
        player.SetTeam(TEAM_ALLIANCE);
        player.SetSelectedDungeons(LfgDungeonSet{ 258 });
        player.SetRoles(PLAYER_ROLE_DAMAGE);
        player.SetComment("Queued solo");
        player.SetState(LFG_STATE_QUEUED);
        _manager->GetQueue(guid).AddQueueData(guid, 0, LfgDungeonSet{ 12 },
            LfgRolesMap{ { guid, PLAYER_ROLE_DAMAGE } });
        _manager->LeaveLfg(guid);
        _manager->LeaveAllLfgQueues(guid, true);
    }

    void RemoveMemberDuringReplacementSearch(ObjectGuid guid)
    {
        // Match LFGGroupScript::OnRemoveMember without a live player/session.
        _manager->LeaveLfg(guid);
        _manager->LeaveAllLfgQueues(guid, true, _group);
        _manager->SetGroup(guid, ObjectGuid::Empty);
        _manager->RemovePlayerFromGroup(_group, guid);
    }

    LFGMgr* _manager = nullptr;
    std::unique_ptr<IWorld> _previousWorld;
    ObjectGuid const _group = ObjectGuid::Create<HighGuid::Group>(900000);
    std::array<ObjectGuid, 4> const _players = {
        ObjectGuid::Create<HighGuid::Player>(900001), ObjectGuid::Create<HighGuid::Player>(900002),
        ObjectGuid::Create<HighGuid::Player>(900003), ObjectGuid::Create<HighGuid::Player>(900004)
    };
};

TEST_F(LfgQueueCancellationTest, ReplacementCancellationPreservesRunAndRewardSelection)
{
    PrepareGroup(true);
    CancelGroupQueue();

    EXPECT_EQ(_manager->GetState(_group), LFG_STATE_DUNGEON);
    EXPECT_EQ(_manager->GetDungeon(_group), 12u);
    EXPECT_EQ(_manager->GetKicksLeft(_group), 4u);
    for (std::size_t i = 0; i < _players.size(); ++i)
    {
        ObjectGuid guid = _players[i];
        EXPECT_EQ(_manager->GetState(guid), LFG_STATE_DUNGEON);
        EXPECT_EQ(_manager->GetSelectedDungeons(guid), (LfgDungeonSet{ i == 0 ? 258u : 12u }));
        EXPECT_EQ(_manager->GetRoles(guid), PLAYER_ROLE_DAMAGE);
        EXPECT_EQ(_manager->GetRandomPlayersCount(guid), 3u);
    }
}

TEST_F(LfgQueueCancellationTest, RepeatedReplacementCancellationPreservesTheRun)
{
    PrepareGroup(true);
    CancelGroupQueue();
    QueueGroup();
    CancelGroupQueue();

    EXPECT_EQ(_manager->GetState(_group), LFG_STATE_DUNGEON);
    EXPECT_EQ(_manager->GetDungeon(_group), 12u);
    EXPECT_EQ(_manager->GetKicksLeft(_group), 4u);
    EXPECT_EQ(_manager->GetSelectedDungeons(_players.front()), (LfgDungeonSet{ 258 }));
}

TEST_F(LfgQueueCancellationTest, OrdinaryPremadeCancellationClearsQueueData)
{
    PrepareGroup(false);
    CancelGroupQueue();

    EXPECT_EQ(_manager->GetState(_group), LFG_STATE_NONE);
    for (ObjectGuid guid : _players)
    {
        EXPECT_EQ(_manager->GetState(guid), LFG_STATE_NONE);
        EXPECT_TRUE(_manager->GetSelectedDungeons(guid).empty());
        EXPECT_EQ(_manager->GetRoles(guid), PLAYER_ROLE_NONE);
        EXPECT_TRUE(_manager->GetComment(guid).empty());
    }
}

TEST_F(LfgQueueCancellationTest, OrdinarySoloCancellationClearsQueueData)
{
    PrepareAndCancelSoloQueue();

    ObjectGuid guid = _players.front();
    EXPECT_EQ(_manager->GetState(guid), LFG_STATE_NONE);
    EXPECT_TRUE(_manager->GetSelectedDungeons(guid).empty());
    EXPECT_EQ(_manager->GetRoles(guid), PLAYER_ROLE_NONE);
    EXPECT_TRUE(_manager->GetComment(guid).empty());
}

TEST_F(LfgQueueCancellationTest, DisbandAfterReplacementCancellationClearsRewardSelection)
{
    PrepareGroup(true);
    CancelGroupQueue();
    _manager->RemoveGroupData(_group);

    for (ObjectGuid guid : _players)
    {
        EXPECT_EQ(_manager->GetState(guid), LFG_STATE_NONE);
        EXPECT_TRUE(_manager->GetSelectedDungeons(guid).empty());
        EXPECT_EQ(_manager->GetGroup(guid), ObjectGuid::Empty);
    }
}

TEST_F(LfgQueueCancellationTest, MemberDepartureClearsOnlyTheDepartingPlayersSelection)
{
    PrepareGroup(true);
    ObjectGuid departed = _players.back();
    RemoveMemberDuringReplacementSearch(departed);

    EXPECT_EQ(_manager->GetState(departed), LFG_STATE_NONE);
    EXPECT_TRUE(_manager->GetSelectedDungeons(departed).empty());
    EXPECT_EQ(_manager->GetGroup(departed), ObjectGuid::Empty);
    EXPECT_EQ(_manager->GetState(_group), LFG_STATE_DUNGEON);
    EXPECT_EQ(_manager->GetDungeon(_group), 12u);
    EXPECT_EQ(_manager->GetKicksLeft(_group), 4u);
    EXPECT_EQ(_manager->GetSelectedDungeons(_players.front()), (LfgDungeonSet{ 258 }));
    for (ObjectGuid guid : _players)
        if (guid != departed)
            EXPECT_EQ(_manager->GetState(guid), LFG_STATE_DUNGEON);
}
}
