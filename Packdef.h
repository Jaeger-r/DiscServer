#ifndef PACKDEF_H
#define PACKDEF_H

#include <cstdint>

#define _default_protocol_base  10

//注册
#define _default_protocol_register_request                _default_protocol_base +1
#define _default_protocol_register_send                   _default_protocol_base +2
//登录
#define _default_protocol_login_request                   _default_protocol_base +3
#define _default_protocol_login_send                      _default_protocol_base +4
//获取文件列表
#define _default_protocol_getfilelist_request             _default_protocol_base +5
#define _default_protocol_getfilelist_send                _default_protocol_base +6
//上传文件  正常传文件，断点续传、秒传
#define _default_protocol_uploadfileinfo_request          _default_protocol_base +7
#define _default_protocol_uploadfileinfo_send             _default_protocol_base +8
#define _default_protocol_uoloadfileblock_request         _default_protocol_base +9
#define _default_protocol_uoloadfileblock_send            _default_protocol_base +10
//下载文件  正常传文件，断点续传、秒传
#define _default_protocol_downloadfileinfo_request        _default_protocol_base +11
#define _default_protocol_downloadfileinfo_send           _default_protocol_base +12
#define _default_protocol_downloadfileblock_request       _default_protocol_base +13
#define _default_protocol_downloadfileblock_send          _default_protocol_base +14
//搜索文件
#define _default_protocol_searchfile_request              _default_protocol_base +15
#define _default_protocol_searchfile_send                 _default_protocol_base +16
//删除文件
#define _default_protocol_deletefile_request              _default_protocol_base +17
#define _default_protocol_deletefile_send                 _default_protocol_base +18
//分享
#define _default_protocol_sharelink_request               _default_protocol_base + 19
#define _default_protocol_sharelink_send                  _default_protocol_base + 20
//提取
#define _default_protocol_getlink_request                 _default_protocol_base + 21
#define _default_protocol_getlink_send                    _default_protocol_base + 22
//聊天
#define _default_protocol_chat_request                    _default_protocol_base + 23
#define _default_protocol_chat_send                       _default_protocol_base + 24
//传输控制
#define _default_protocol_transfercontrol_request         _default_protocol_base + 25
#define _default_protocol_transfercontrol_send            _default_protocol_base + 26
//重命名
#define _default_protocol_renamefile_request             _default_protocol_base + 27
#define _default_protocol_renamefile_send                _default_protocol_base + 28
//同账号文件同步通知
#define _default_protocol_filesync_send                  _default_protocol_base + 29
//在线用户和私聊
#define _default_protocol_online_users_request           _default_protocol_base + 30
#define _default_protocol_online_users_send              _default_protocol_base + 31
#define _default_protocol_private_chat_request           _default_protocol_base + 32
#define _default_protocol_private_chat_send              _default_protocol_base + 33
#define _default_protocol_private_history_request        _default_protocol_base + 34
#define _default_protocol_private_history_send           _default_protocol_base + 35
#define _default_protocol_profile_update_request         _default_protocol_base + 36
#define _default_protocol_profile_update_send            _default_protocol_base + 37
//客户端版本检查
#define _default_protocol_version_check_request          _default_protocol_base + 38
#define _default_protocol_version_check_send             _default_protocol_base + 39
//安全区防线
#define _default_protocol_game_join_request              _default_protocol_base + 40
#define _default_protocol_game_action_request            _default_protocol_base + 41
#define _default_protocol_game_state_send                _default_protocol_base + 42
#define _default_protocol_game_event_send                _default_protocol_base + 43
#define _default_protocol_game_state_compressed_send     _default_protocol_base + 44
#define _default_protocol_earth_chunk_request            _default_protocol_base + 45
#define _default_protocol_earth_chunk_send               _default_protocol_base + 46
#define _default_protocol_earth_chunk_compressed_send    _default_protocol_base + 47
#define _default_protocol_earth_overview_request         _default_protocol_base + 48
#define _default_protocol_earth_overview_send            _default_protocol_base + 49
#define _default_protocol_earth_overview_compressed_send _default_protocol_base + 50



//协议包

#define MAXSIZE 128
#define NAMESIZE 256
#define FILENUM 15
#define SQLLEN  300
#define ONE_PAGE 65536
#define MAXSENDMESSSAGE 1024
#define ONLINEUSERNUM 64
#define CHATHISTORYNUM 50
#define VERSION_SIZE 32
#define UPDATE_URL_SIZE 256
#define UPDATE_NOTES_SIZE 512
#define UPDATE_SHA256_SIZE 65
#define GAME_MAP_WIDTH 100
#define GAME_MAP_HEIGHT 100
#define GAME_MAX_PLAYERS 16
#define GAME_MAX_ZOMBIES 384
#define GAME_MAX_ZONES 64
#define GAME_NAME_SIZE 48
#define GAME_EVENT_SIZE 192
#define EARTH_CHUNK_SIZE 64
#define EARTH_CHUNK_CELL_COUNT (EARTH_CHUNK_SIZE * EARTH_CHUNK_SIZE)
#define EARTH_MAX_LABELS 24
#define EARTH_LABEL_NAME_SIZE 64
#define EARTH_WORLD_WIDTH 34668
#define EARTH_WORLD_HEIGHT 14713
#define EARTH_CELL_SIZE_KM 1.0f
#define EARTH_STREAM_RADIUS_CHUNKS 6
#define EARTH_BATTLE_REGION_SIZE_CELLS 10
#define EARTH_OVERVIEW_SAMPLE_CELLS 32
#define EARTH_OVERVIEW_WIDTH ((EARTH_WORLD_WIDTH + EARTH_OVERVIEW_SAMPLE_CELLS - 1) / EARTH_OVERVIEW_SAMPLE_CELLS)
#define EARTH_OVERVIEW_HEIGHT ((EARTH_WORLD_HEIGHT + EARTH_OVERVIEW_SAMPLE_CELLS - 1) / EARTH_OVERVIEW_SAMPLE_CELLS)

#define _earth_terrain_ocean 0
#define _earth_terrain_land 1
#define _earth_terrain_river 2
#define GAME_ZOMBIE_BASE_ATTACK 18
#define GAME_ZOMBIE_ELITE_ATTACK_BONUS 4
#define GAME_ZOMBIE_KIND_NORMAL 0
#define GAME_ZOMBIE_KIND_ELITE 1
#define GAME_ZOMBIE_KIND_GIANT 2
#define GAME_GIANT_ZOMBIE_ATTACK 42
#define GAME_GIANT_ZOMBIE_HP_BONUS 220
#define GAME_GIANT_ZOMBIE_EXPLOSION_RADIUS 3.0f
#define GAME_GIANT_ZOMBIE_EXPLOSION_PLAYER_DAMAGE 40
#define GAME_GIANT_ZOMBIE_EXPLOSION_BUILDING_DAMAGE 55
#define GAME_WALL_MAX_HP 120
#define GAME_TURRET_MAX_HP 80
#define GAME_TURRET_ATTACK 15
#define GAME_TURRET_RANGE 4
#define GAME_TURRET_FIRE_INTERVAL_MS 1000
#define GAME_TURRET_SPLASH_UNLOCK_LEVEL 5
#define GAME_TURRET_SPLASH_RADIUS 1.25f
#define GAME_TURRET_SPLASH_DAMAGE_PERCENT 50
#define GAME_HEAVY_TURRET_MAX_HP 70
#define GAME_HEAVY_TURRET_ATTACK 42
#define GAME_HEAVY_TURRET_RANGE 8
#define GAME_HEAVY_TURRET_FIRE_INTERVAL_MS 3000
#define GAME_HEAVY_TURRET_ATTACK_PER_LEVEL 10
#define GAME_HEAVY_TURRET_RANGE_LEVEL_STEP 4
#define GAME_PLAYER_ATTACK 25
#define GAME_PLAYER_ATTACK_RANGE 1.35f
#define GAME_PLAYER_FIRE_INTERVAL_MS 500
#define GAME_PLAYER_ATTACK_PER_LEVEL 1
#define GAME_PLAYER_RANGE_LEVEL_STEP 8
#define GAME_PLAYER_RANGE_PER_STEP 0.15f
#define GAME_PLAYER_FIRE_INTERVAL_REDUCTION_PER_LEVEL_MS 3
#define GAME_PLAYER_MIN_FIRE_INTERVAL_MS 250
#define GAME_PLAYER_EXP_BASE 5
#define GAME_PLAYER_EXP_PER_LEVEL 3
#define GAME_PLAYER_KILL_EXP 1
#define GAME_PLAYER_ELITE_EXP_BONUS 1
#define GAME_PLAYER_RESPAWN_SECONDS 10
#define GAME_PLAYER_RESPAWN_INVULNERABLE_SECONDS 5
#define GAME_PLAYER_TERRITORY_REGEN_HP 2
#define GAME_PLAYER_TERRITORY_REGEN_INTERVAL_MS 2000
#define GAME_PLAYER_START_COINS 180
#define GAME_PLAYER_START_STONE 40
#define GAME_MAX_LEVEL 100
#define GAME_ZOMBIE_COIN_REWARD 5
#define GAME_ZOMBIE_ELITE_COIN_BONUS 2
#define GAME_WAVE_INTERVAL_SECONDS 60
#define GAME_WAVE_ASSAULT_SECONDS 45
#define GAME_BUILDING_HP_PER_LEVEL_PERCENT 25
#define GAME_TURRET_ATTACK_PER_LEVEL 5
#define GAME_BUILDING_EXP_BASE 8
#define GAME_BUILDING_EXP_PER_LEVEL 4
#define GAME_BUILDING_UPGRADE_BASE_COST 20
#define GAME_BUILDING_UPGRADE_COST_PER_LEVEL 15
#define GAME_BUILDING_REPAIR_HP_PER_COIN 10
#define GAME_BUILDING_REPAIR_HP_PER_TICK 10
#define GAME_BUILDING_REPAIR_INTERVAL_MS 1000
#define GAME_EXTRACTION_INTERVAL_MS 5000
#define GAME_EXTRACTOR_MAX_HP 90
#define GAME_COIN_COLLECTOR_YIELD 12
#define GAME_INITIAL_POPULATION 10
#define GAME_BASE_POPULATION_CAPACITY 10
#define GAME_BASE_FOOD_CAPACITY 10
#define GAME_BASE_WATER_CAPACITY 10
#define GAME_HOUSE_MAX_LEVEL 10
#define GAME_HOUSE_MAX_HP 110
#define GAME_FARM_MAX_HP 70
#define GAME_WELL_MAX_HP 90
#define GAME_FARM_BASE_PEOPLE 6
#define GAME_WELL_BASE_PEOPLE 8
#define GAME_FARM_PEOPLE_PER_LEVEL 1
#define GAME_WELL_PEOPLE_PER_LEVEL 1
#define GAME_POPULATION_INTERVAL_MS 30000
#define GAME_WAVE_SHIELD_MAX_HP 180
#define GAME_WAVE_SHIELD_RADIUS 3.0f

#define GAME_DISASTER_MIN_INTERVAL_SECONDS 180
#define GAME_DISASTER_MAX_INTERVAL_SECONDS 420
#define GAME_DISASTER_OUTBREAK_BASE_ZOMBIES 28
#define GAME_DISASTER_OUTBREAK_ZOMBIES_PER_WAVE 3
#define GAME_DISASTER_OUTBREAK_MAX_ZOMBIES 72
#define GAME_DISASTER_LIGHTNING_NEAR_TERRITORY_PERCENT 85
#define GAME_DISASTER_LIGHTNING_RADIUS 2.5f
#define GAME_DISASTER_LIGHTNING_PLAYER_DAMAGE 35
#define GAME_DISASTER_LIGHTNING_BUILDING_DAMAGE 45
#define GAME_DISASTER_LIGHTNING_ZOMBIE_DAMAGE 80

#define GAME_WALL_COIN_COST 5
#define GAME_WALL_STONE_COST 4
#define GAME_DOOR_COIN_COST 6
#define GAME_DOOR_STONE_COST 4
#define GAME_TURRET_COIN_COST 35
#define GAME_HEAVY_TURRET_COIN_COST 90
#define GAME_HEAVY_TURRET_IRON_COST 12
#define GAME_HEAVY_TURRET_COAL_COST 8
#define GAME_HEAVY_TURRET_OIL_COST 4
#define GAME_STONE_EXTRACTOR_COIN_COST 20
#define GAME_ALUMINUM_EXTRACTOR_COIN_COST 30
#define GAME_ALUMINUM_EXTRACTOR_STONE_COST 15
#define GAME_IRON_EXTRACTOR_COIN_COST 40
#define GAME_IRON_EXTRACTOR_ALUMINUM_COST 12
#define GAME_COAL_EXTRACTOR_COIN_COST 50
#define GAME_COAL_EXTRACTOR_IRON_COST 10
#define GAME_OIL_EXTRACTOR_COIN_COST 65
#define GAME_OIL_EXTRACTOR_COAL_COST 12
#define GAME_COIN_COLLECTOR_COIN_COST 45
#define GAME_COIN_COLLECTOR_STONE_COST 8
#define GAME_HOUSE_COIN_COST 30
#define GAME_HOUSE_STONE_COST 10
#define GAME_FARM_COIN_COST 25
#define GAME_WELL_COIN_COST 30
#define GAME_WELL_STONE_COST 8
#define GAME_WAVE_SHIELD_COIN_COST 180
#define GAME_WAVE_SHIELD_ALUMINUM_COST 20
#define GAME_WAVE_SHIELD_IRON_COST 30
#define GAME_WAVE_SHIELD_COAL_COST 20
#define GAME_WAVE_SHIELD_OIL_COST 12

#define _game_action_move 1
#define _game_action_attack 2
#define _game_action_build 3
#define _game_action_demolish 4
#define _game_action_leave 5
#define _game_action_move_target 6
#define _game_action_upgrade 7
#define _game_action_repair 8
#define _game_action_set_respawn_zone 9

#define _game_building_none 0
#define _game_building_wall 1
#define _game_building_turret 2
#define _game_building_door 3
#define _game_building_heavy_turret 4
#define _game_building_stone_extractor 5
#define _game_building_aluminum_extractor 6
#define _game_building_iron_extractor 7
#define _game_building_coal_extractor 8
#define _game_building_oil_extractor 9
#define _game_building_coin_collector 10
#define _game_building_house 11
#define _game_building_farm 12
#define _game_building_well 13
#define _game_building_wave_shield 14

#define _game_terrain_plain 0
#define _game_terrain_mountain 1

#define _game_resource_none 0
#define _game_resource_stone 1
#define _game_resource_aluminum 2
#define _game_resource_iron 3
#define _game_resource_coal 4
#define _game_resource_oil 5

#define _game_zone_none 0
#define _game_zone_stabilizing 1
#define _game_zone_permanent 2

#define _game_event_info 1
#define _game_event_success 2
#define _game_event_warning 3
#define _game_event_wave 4
#define _game_event_explosion 5
#define _game_event_turret_explosion 6
#define _game_event_lightning 7
#define _register_err 0
#define _register_success 1

#define _login_usernoexist 0
#define _login_passworderr 1
#define _login_success 2

#define _profile_update_success 0
#define _profile_update_fail 1
#define _profile_update_name_exists 2
#define _profile_update_invalid 3

#define _fileinfo_isuploaded 0       //已经上传
#define _fileinfo_continue   1       //断点续传
#define _fileinfo_speedtransfer 2    //秒传
#define _fileinfo_normal     3       //正常传
#define _fileinfo_busy       4       //被其他终端占用

#define _filestate_ready      0
#define _filestate_uploading  1
#define _filestate_incomplete 2
#define _filestate_abnormal   3

#define _delete_success 0
#define _delete_fail 1
#define _delete_noexit 2

#define _rename_success 0
#define _rename_fail 1
#define _rename_noexit 2

#define _filesync_action_upload_started   1
#define _filesync_action_upload_completed 2
#define _filesync_action_delete           3
#define _filesync_action_rename           4
#define _filesync_action_upload_paused    5
#define _filesync_action_upload_cancelled 6

#define _transfer_target_upload 1
#define _transfer_target_download 2

#define _transfer_action_pause 1
#define _transfer_action_cancel 2

#define _transfer_result_failed 0
#define _transfer_result_running 1
#define _transfer_result_finished 2


struct STRU_BASE{
    char m_nType;//包类型
};

/*
    申请账号
*/
struct STRU_REGISTER_RQ:public STRU_BASE{
    STRU_REGISTER_RQ(){
        m_nType = _default_protocol_register_request;
    }
    char m_szName[MAXSIZE];
    char m_szPassWord[MAXSIZE];
    long long m_tel;
};
struct STRU_REGISTER_RS:public STRU_BASE{
    STRU_REGISTER_RS(){
        m_nType = _default_protocol_register_send;
    }
    char m_szResult;
};

/*
    登录
*/
struct STRU_LOGIN_RQ:public STRU_BASE{
    STRU_LOGIN_RQ(){
        m_nType = _default_protocol_login_request;
    }
    char m_szName[MAXSIZE];
    char m_szPassWord[MAXSIZE];
};
struct STRU_LOGIN_RS:public STRU_BASE{
    STRU_LOGIN_RS(){
        m_nType = _default_protocol_login_send;
    }
    long long m_userId;
    char m_szResult;
};


/*
    获取文件列表
*/
struct STRU_GETFILELIST_RQ:public STRU_BASE{
    STRU_GETFILELIST_RQ(){
        m_nType = _default_protocol_getfilelist_request;
    }
    long long m_userId;
};
struct FileInfo{
    char m_szFileName[MAXSIZE];
    char m_szFileDateTime[MAXSIZE];
    long long m_fileSize;
    char m_szFileMD5[MAXSIZE];
    char m_fileState;
};
struct STRU_GETFILELIST_RS:public STRU_BASE{
    STRU_GETFILELIST_RS(){
        m_nType = _default_protocol_getfilelist_send;
    }
    FileInfo m_aryInfo[FILENUM];
    long m_FileNum;
};
//上传文件
struct STRU_UPLOADFILEINFO_RQ : public STRU_BASE{
    STRU_UPLOADFILEINFO_RQ(){
        m_nType = _default_protocol_uploadfileinfo_request;
    }
    long long m_userid;
    char m_szFileName[MAXSIZE];
    long long m_filesize;
    char m_szFileMD5[MAXSIZE];
};
struct STRU_UPLOADFILEINFO_RS : public STRU_BASE{
    STRU_UPLOADFILEINFO_RS(){
        m_nType = _default_protocol_uploadfileinfo_send;
    }
    char m_szFileName[MAXSIZE];
    char m_szFileMD5[MAXSIZE];
    long long m_fileId;
    long long m_pos;
    char m_szResult;
};
struct STRU_UPLOADFILEBLOCK_RQ : public STRU_BASE{
    STRU_UPLOADFILEBLOCK_RQ(){
        m_nType = _default_protocol_uoloadfileblock_request;
    }
    long long m_fileId;
    char m_szFileContent[ONE_PAGE];
    long m_fileNum;
};
struct STRU_UPLOADFILEBLOCK_RS : public STRU_BASE{
    STRU_UPLOADFILEBLOCK_RS(){
        m_nType = _default_protocol_uoloadfileblock_send;
    }
    long long m_fileId;
    long long m_pos;
    long long m_fileSize;
    char m_szResult;
};
//下载文件
struct STRU_DOWNLOADFILEINFO_RQ : public STRU_BASE{
    STRU_DOWNLOADFILEINFO_RQ(){
        m_nType = _default_protocol_downloadfileinfo_request;
    }
    long long m_userid;
    char m_szFileName[MAXSIZE];
    long long m_filesize;
    char m_szFileMD5[MAXSIZE];
    long long m_pos;
};
struct STRU_DOWNLOADFILEINFO_RS : public STRU_BASE{
    STRU_DOWNLOADFILEINFO_RS(){
        m_nType = _default_protocol_downloadfileinfo_send;
    }
    char m_szFileName[MAXSIZE];
    char m_szFileMD5[MAXSIZE];
    long long m_fileId;
    long long m_pos;
    char m_szResult;
};
struct STRU_DOWNLOADFILEBLOCK_RQ : public STRU_BASE{
    STRU_DOWNLOADFILEBLOCK_RQ(){
        m_nType = _default_protocol_downloadfileblock_request;
    }
    long long m_fileId;
    char m_szFileContent[ONE_PAGE];
    long m_fileNum;
};
struct STRU_DOWNLOADFILEBLOCK_RS : public STRU_BASE{
    STRU_DOWNLOADFILEBLOCK_RS(){
        m_nType = _default_protocol_downloadfileblock_send;
    }
    long long m_fileId;
    char m_szFileContent[ONE_PAGE];
    long m_fileNum;
};
//删除文件
struct STRU_DELETEFILE_RQ:public STRU_BASE{
    STRU_DELETEFILE_RQ(){
        m_nType = _default_protocol_deletefile_request;
    }
    long long m_userId;
    char m_szFileMD5[MAXSIZE];
};
struct STRU_DELETEFILE_RS:public STRU_BASE{
    STRU_DELETEFILE_RS(){
        m_nType = _default_protocol_deletefile_send;
    }
    long long m_userId;
    char m_szResult;
    char m_szFileMD5[MAXSIZE];
};
//重命名
struct STRU_RENAMEFILE_RQ:public STRU_BASE{
    STRU_RENAMEFILE_RQ(){
        m_nType = _default_protocol_renamefile_request;
    }
    long long m_userId;
    char m_szFileMD5[MAXSIZE];
    char m_szNewFileName[NAMESIZE];
};
struct STRU_RENAMEFILE_RS:public STRU_BASE{
    STRU_RENAMEFILE_RS(){
        m_nType = _default_protocol_renamefile_send;
    }
    long long m_userId;
    char m_szResult;
    char m_szFileMD5[MAXSIZE];
    char m_szNewFileName[NAMESIZE];
};
//同账号文件同步通知
struct STRU_FILESYNC_RS:public STRU_BASE{
    STRU_FILESYNC_RS(){
        m_nType = _default_protocol_filesync_send;
    }
    long long m_userId;
    char m_action;
    char m_szFileMD5[MAXSIZE];
    char m_szFileName[NAMESIZE];
};
//聊天
struct STRU_CHAT_RQ:public STRU_BASE{
    STRU_CHAT_RQ(){
        m_nType = _default_protocol_chat_request;
    }
    char m_userName[MAXSIZE];
    char szbuf[MAXSENDMESSSAGE];
};
struct STRU_CHAT_RS:public STRU_BASE{
    STRU_CHAT_RS(){
        m_nType = _default_protocol_chat_send;
    }
    char m_userName[MAXSIZE];
    char szbuf[MAXSENDMESSSAGE];
};
struct OnlineUserInfo {
    long long m_userId;
    char m_userName[MAXSIZE];
    char m_online;
};
struct STRU_ONLINE_USERS_RQ:public STRU_BASE{
    STRU_ONLINE_USERS_RQ(){
        m_nType = _default_protocol_online_users_request;
    }
    long long m_userId;
};
struct STRU_ONLINE_USERS_RS:public STRU_BASE{
    STRU_ONLINE_USERS_RS(){
        m_nType = _default_protocol_online_users_send;
    }
    long long m_userId;
    int m_userCount;
    OnlineUserInfo m_users[ONLINEUSERNUM];
};
struct STRU_PRIVATE_CHAT_RQ:public STRU_BASE{
    STRU_PRIVATE_CHAT_RQ(){
        m_nType = _default_protocol_private_chat_request;
    }
    long long m_senderId;
    long long m_receiverId;
    char m_senderName[MAXSIZE];
    char m_receiverName[MAXSIZE];
    char szbuf[MAXSENDMESSSAGE];
};
struct STRU_PRIVATE_CHAT_RS:public STRU_BASE{
    STRU_PRIVATE_CHAT_RS(){
        m_nType = _default_protocol_private_chat_send;
    }
    long long m_senderId;
    long long m_receiverId;
    char m_senderName[MAXSIZE];
    char m_receiverName[MAXSIZE];
    char szbuf[MAXSENDMESSSAGE];
    char m_offline;
};
struct ChatHistoryInfo {
    long long m_senderId;
    long long m_receiverId;
    char m_senderName[MAXSIZE];
    char m_receiverName[MAXSIZE];
    char m_createdAt[MAXSIZE];
    char szbuf[MAXSENDMESSSAGE];
};
struct STRU_PRIVATE_HISTORY_RQ:public STRU_BASE{
    STRU_PRIVATE_HISTORY_RQ(){
        m_nType = _default_protocol_private_history_request;
    }
    long long m_userId;
    long long m_peerId;
};
struct STRU_PRIVATE_HISTORY_RS:public STRU_BASE{
    STRU_PRIVATE_HISTORY_RS(){
        m_nType = _default_protocol_private_history_send;
    }
    long long m_userId;
    long long m_peerId;
    int m_messageCount;
    ChatHistoryInfo m_messages[CHATHISTORYNUM];
};
struct STRU_PROFILE_UPDATE_RQ:public STRU_BASE{
    STRU_PROFILE_UPDATE_RQ(){
        m_nType = _default_protocol_profile_update_request;
    }
    long long m_userId;
    char m_szName[MAXSIZE];
    char m_szPassWord[MAXSIZE];
};
struct STRU_PROFILE_UPDATE_RS:public STRU_BASE{
    STRU_PROFILE_UPDATE_RS(){
        m_nType = _default_protocol_profile_update_send;
    }
    long long m_userId;
    char m_szResult;
    char m_szName[MAXSIZE];
};
struct STRU_VERSION_CHECK_RQ:public STRU_BASE{
    STRU_VERSION_CHECK_RQ(){
        m_nType = _default_protocol_version_check_request;
    }
    char m_currentVersion[VERSION_SIZE];
    char m_platform[VERSION_SIZE];
};
struct STRU_VERSION_CHECK_RS:public STRU_BASE{
    STRU_VERSION_CHECK_RS(){
        m_nType = _default_protocol_version_check_send;
    }
    char m_updateAvailable;
    char m_forceUpdate;
    char m_currentVersion[VERSION_SIZE];
    char m_latestVersion[VERSION_SIZE];
    char m_downloadUrl[UPDATE_URL_SIZE];
    char m_releaseNotes[UPDATE_NOTES_SIZE];
    char m_sha256[UPDATE_SHA256_SIZE];
};

#pragma pack(push, 1)
struct STRU_GAME_JOIN_RQ {
    STRU_GAME_JOIN_RQ()
        : m_nType(_default_protocol_game_join_request)
    {
    }
    std::uint8_t m_nType;
    std::int64_t m_userId = 0;
    std::uint8_t m_colorR = 38;
    std::uint8_t m_colorG = 166;
    std::uint8_t m_colorB = 154;
};

struct STRU_GAME_ACTION_RQ {
    STRU_GAME_ACTION_RQ()
        : m_nType(_default_protocol_game_action_request)
    {
    }
    std::uint8_t m_nType;
    std::int64_t m_userId = 0;
    std::uint8_t m_action = 0;
    float m_x = 0.0f;
    float m_y = 0.0f;
    std::uint8_t m_buildingType = _game_building_none;
};

struct GameCellInfo {
    std::int32_t m_zoneId = 0;
    std::int64_t m_buildingOwnerId = 0;
    std::int16_t m_buildingHp = 0;
    std::uint8_t m_terrain = 0;
    std::uint8_t m_resourceType = _game_resource_none;
    std::int16_t m_resourceAmount = 0;
    std::uint8_t m_buildingType = _game_building_none;
    std::uint8_t m_buildingLevel = 1;
    std::int16_t m_buildingExp = 0;
    std::uint8_t m_dormantZombieDensity = 0;
    std::int8_t m_dormantFlowX = 0;
    std::int8_t m_dormantFlowY = 0;
};

struct GamePlayerInfo {
    std::int64_t m_userId = 0;
    char m_userName[GAME_NAME_SIZE] = {0};
    float m_x = 0.0f;
    float m_y = 0.0f;
    std::int16_t m_hp = 0;
    std::int16_t m_maxHp = 100;
    std::int32_t m_coins = 0;
    std::int32_t m_stone = 0;
    std::int32_t m_aluminum = 0;
    std::int32_t m_iron = 0;
    std::int32_t m_coal = 0;
    std::int32_t m_oil = 0;
    std::int32_t m_respawnRemainingSeconds = 0;
    std::int32_t m_invulnerableRemainingSeconds = 0;
    std::int16_t m_level = 1;
    std::int32_t m_kills = 0;
    std::int32_t m_experience = 0;
    std::int32_t m_experienceToNextLevel = 0;
    std::int32_t m_respawnZoneId = 0;
    std::int32_t m_population = GAME_INITIAL_POPULATION;
    std::int32_t m_populationCapacity = GAME_BASE_POPULATION_CAPACITY;
    std::int32_t m_populationUsed = 0;
    std::int32_t m_foodCapacity = GAME_BASE_FOOD_CAPACITY;
    std::int32_t m_waterCapacity = GAME_BASE_WATER_CAPACITY;
    std::uint8_t m_online = 0;
    std::uint8_t m_dead = 0;
    std::uint8_t m_colorR = 38;
    std::uint8_t m_colorG = 166;
    std::uint8_t m_colorB = 154;
};

struct GameTerritoryRankInfo {
    std::int64_t m_userId = 0;
    char m_userName[GAME_NAME_SIZE] = {0};
    std::int32_t m_permanentCellCount = 0;
    std::uint8_t m_colorR = 38;
    std::uint8_t m_colorG = 166;
    std::uint8_t m_colorB = 154;
};

struct GameZombieInfo {
    std::int32_t m_zombieId = 0;
    float m_x = 0.0f;
    float m_y = 0.0f;
    std::int16_t m_hp = 0;
    std::int16_t m_maxHp = 0;
    std::uint8_t m_kind = 0;
};

struct GameZoneInfo {
    std::int32_t m_zoneId = 0;
    std::int64_t m_ownerId = 0;
    std::int32_t m_cellCount = 0;
    std::int64_t m_stabilizeRemainingSeconds = 0;
    std::int64_t m_reinforceRemainingSeconds = 0;
    std::uint8_t m_state = _game_zone_none;
    std::uint8_t m_shieldLayers = 0;
    std::uint8_t m_colorR = 38;
    std::uint8_t m_colorG = 166;
    std::uint8_t m_colorB = 154;
};

struct STRU_GAME_STATE_RS {
    STRU_GAME_STATE_RS()
        : m_nType(_default_protocol_game_state_send)
    {
    }
    std::uint8_t m_nType;
    std::int64_t m_serverTimeMs = 0;
    std::int32_t m_wave = 0;
    std::int32_t m_nextWaveSeconds = 0;
    std::int32_t m_playerCount = 0;
    std::int32_t m_zombieCount = 0;
    std::int32_t m_zoneCount = 0;
    std::int32_t m_territoryRankCount = 0;
    GameCellInfo m_cells[GAME_MAP_WIDTH * GAME_MAP_HEIGHT];
    GamePlayerInfo m_players[GAME_MAX_PLAYERS];
    GameZombieInfo m_zombies[GAME_MAX_ZOMBIES];
    GameZoneInfo m_zones[GAME_MAX_ZONES];
    GameTerritoryRankInfo m_territoryRanks[GAME_MAX_PLAYERS];
};

struct STRU_GAME_EVENT_RS {
    STRU_GAME_EVENT_RS()
        : m_nType(_default_protocol_game_event_send)
    {
    }
    std::uint8_t m_nType;
    std::int64_t m_userId = 0;
    std::uint8_t m_eventType = _game_event_info;
    float m_x = -1.0f;
    float m_y = -1.0f;
    char m_message[GAME_EVENT_SIZE] = {0};
};

struct STRU_EARTH_CHUNK_RQ {
    STRU_EARTH_CHUNK_RQ()
        : m_nType(_default_protocol_earth_chunk_request)
    {
    }
    std::uint8_t m_nType;
    std::int64_t m_userId = 0;
    std::int32_t m_chunkX = 0;
    std::int32_t m_chunkY = 0;
};

struct STRU_EARTH_OVERVIEW_RQ {
    STRU_EARTH_OVERVIEW_RQ()
        : m_nType(_default_protocol_earth_overview_request)
    {
    }
    std::uint8_t m_nType;
    std::int64_t m_userId = 0;
};

struct EarthMapLabel {
    std::int32_t m_worldX = 0;
    std::int32_t m_worldY = 0;
    std::uint8_t m_rank = 9;
    char m_name[EARTH_LABEL_NAME_SIZE] = {0};
};

struct STRU_EARTH_CHUNK_RS {
    STRU_EARTH_CHUNK_RS()
        : m_nType(_default_protocol_earth_chunk_send)
    {
    }
    std::uint8_t m_nType;
    std::int32_t m_chunkX = 0;
    std::int32_t m_chunkY = 0;
    std::int32_t m_worldWidth = EARTH_WORLD_WIDTH;
    std::int32_t m_worldHeight = EARTH_WORLD_HEIGHT;
    std::int32_t m_playerWorldX = 0;
    std::int32_t m_playerWorldY = 0;
    std::uint16_t m_labelCount = 0;
    std::uint8_t m_cells[EARTH_CHUNK_CELL_COUNT] = {0};
    EarthMapLabel m_labels[EARTH_MAX_LABELS];
};

struct EarthOverviewHeader {
    std::uint8_t m_nType = _default_protocol_earth_overview_send;
    std::int32_t m_worldWidth = EARTH_WORLD_WIDTH;
    std::int32_t m_worldHeight = EARTH_WORLD_HEIGHT;
    std::int32_t m_sampleCells = EARTH_OVERVIEW_SAMPLE_CELLS;
    std::int32_t m_overviewWidth = EARTH_OVERVIEW_WIDTH;
    std::int32_t m_overviewHeight = EARTH_OVERVIEW_HEIGHT;
    std::int32_t m_labelCount = 0;
};
#pragma pack(pop)
static_assert(sizeof(STRU_GAME_JOIN_RQ) == 12, "Unexpected game join packet layout");
static_assert(sizeof(STRU_GAME_ACTION_RQ) == 19, "Unexpected game action packet layout");
static_assert(sizeof(GameCellInfo) == 25, "Unexpected game cell layout");
static_assert(sizeof(GamePlayerInfo) == 143, "Unexpected game player layout");
static_assert(sizeof(GameTerritoryRankInfo) == 63, "Unexpected territory rank layout");
static_assert(sizeof(GameZombieInfo) == 17, "Unexpected game zombie layout");
static_assert(sizeof(GameZoneInfo) == 37, "Unexpected game zone layout");
static_assert(sizeof(STRU_GAME_STATE_RS) == 262225, "Unexpected game state packet layout");
static_assert(sizeof(STRU_GAME_EVENT_RS) == 210, "Unexpected game event packet layout");
static_assert(sizeof(STRU_EARTH_CHUNK_RQ) == 17, "Unexpected Earth chunk request layout");
static_assert(sizeof(STRU_EARTH_OVERVIEW_RQ) == 9, "Unexpected Earth overview request layout");
static_assert(sizeof(EarthMapLabel) == 73, "Unexpected Earth label layout");
static_assert(sizeof(STRU_EARTH_CHUNK_RS) == 5875, "Unexpected Earth chunk packet layout");
static_assert(sizeof(EarthOverviewHeader) == 25, "Unexpected Earth overview header layout");
//传输控制
struct STRU_TRANSFERCONTROL_RQ:public STRU_BASE{
    STRU_TRANSFERCONTROL_RQ(){
        m_nType = _default_protocol_transfercontrol_request;
    }
    char m_target;
    char m_action;
    long long m_fileId;
    char m_szFileMD5[MAXSIZE];
};
struct STRU_TRANSFERCONTROL_RS:public STRU_BASE{
    STRU_TRANSFERCONTROL_RS(){
        m_nType = _default_protocol_transfercontrol_send;
    }
    char m_target;
    char m_action;
    long long m_fileId;
    char m_szResult;
    char m_szFileMD5[MAXSIZE];
};
//分享
struct STRU_SHARELINK_RQ:public STRU_BASE{
    STRU_SHARELINK_RQ(){
        m_nType = _default_protocol_sharelink_request;
    }
    long long m_userId;
    char m_szFileName[MAXSIZE];
};
struct STRU_SHARELINK_RS:public STRU_BASE{
    STRU_SHARELINK_RS(){
        m_nType = _default_protocol_sharelink_send;
    }
    char m_szFileName[MAXSIZE];
    char m_szCode[MAXSIZE];
};
//提取
struct STRU_GETLINK_RQ:public STRU_BASE{
    STRU_GETLINK_RQ(){
        m_nType = _default_protocol_getlink_request;
    }
    long long m_userId;
    char m_szCode[MAXSIZE];
};
struct STRU_GETLINK_RS:public STRU_BASE{
    STRU_GETLINK_RS(){
        m_nType = _default_protocol_getlink_send;
    }
    long long m_userId;
    char m_szFileName[MAXSIZE];
    long long m_FileSize;
    char m_szFileUploadTime[MAXSIZE];
};
/*
 * 用户表user（u_id,u_name,password,u_tel)
 * 文件信息表 file(f_id,f——name,f_size,f_upoloadtime,f_path,f_count,f_md5)
 * user_file(num,u_id,f_id)

*/
#endif // PACKDEF_H












