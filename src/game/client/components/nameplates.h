/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_NAMEPLATES_H
#define GAME_CLIENT_COMPONENTS_NAMEPLATES_H
#include <game/client/component.h>
struct SPlayerNamePlate
{
	SPlayerNamePlate()
	{
		Reset();
	}

	void Reset()
	{
		m_NameTextContainerIndex = m_ClanNameTextContainerIndex = -1;
		m_aName[0] = 0;
		m_aClanName[0] = 0;
		m_NameTextWidth = m_ClanNameTextWidth = 0.f;
		m_NameTextFontSize = m_ClanNameTextFontSize = 0;
		m_RenderColor.r = m_RenderColor.g = m_RenderColor.b = m_RenderColor.a = -1;
		m_RenderColorClan.r = m_RenderColorClan.g = m_RenderColorClan.b = m_RenderColorClan.a = -1;
	}

	char m_aName[MAX_NAME_LENGTH];
	float m_NameTextWidth;
	int m_NameTextContainerIndex;
	float m_NameTextFontSize;

	ColorRGBA m_RenderColor;
	ColorRGBA m_RenderColorClan;

	char m_aClanName[MAX_CLAN_LENGTH];
	float m_ClanNameTextWidth;
	int m_ClanNameTextContainerIndex;
	float m_ClanNameTextFontSize;
};

class CNamePlates : public CComponent
{
	void MapscreenToGroup(float CenterX, float CenterY, CMapItemGroup *pGroup);

	void RenderNameplate(
		const CNetObj_Character *pPrevChar,
		const CNetObj_Character *pPlayerChar,
		const CNetObj_PlayerInfo *pPlayerInfo);
	void RenderNameplatePos(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha);

	SPlayerNamePlate m_aNamePlates[MAX_CLIENTS];
	class CPlayers *m_pPlayers;

	void ResetNamePlates();

public:
	virtual void OnWindowResize();
	virtual void OnInit();
	virtual void OnRender();

	void SetPlayers(class CPlayers *pPlayers);
};

#endif
