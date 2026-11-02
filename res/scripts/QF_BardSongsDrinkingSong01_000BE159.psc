;BEGIN FRAGMENT CODE - Do not edit anything between this and the end comment
;NEXT FRAGMENT INDEX 4
Scriptname QF_BardSongsDrinkingSong01_000BE159 Extends Quest Hidden

;BEGIN ALIAS PROPERTY Lurbuk
;ALIAS PROPERTY TYPE referencealias
referencealias Property Alias_Lurbuk Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Bard
;ALIAS PROPERTY TYPE referencealias
referencealias Property Alias_Bard Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Karita
;ALIAS PROPERTY TYPE referencealias
referencealias Property Alias_Karita Auto
;END ALIAS PROPERTY

;BEGIN ALIAS PROPERTY Location
;ALIAS PROPERTY TYPE locationalias
locationalias Property Alias_Location Auto
;END ALIAS PROPERTY

;BEGIN FRAGMENT Fragment_1
Function Fragment_1()
;BEGIN CODE
;BardSongsKeyword.SendStoryEvent(akref1 = Alias_Bard.GetActorRef())
;BardSongs_Bard.forcerefto(alias_Bard.GetActorRef())

;-----------------------------------------------------------------------------------------------
If Alias_Bard.GetRef() == Alias_Karita.GetRef() && DA16.GetStage() >= 10
     Alias_Karita.GetActorRef().RemoveFromFaction(BardSingerNoAutostart)
Endif

;-----------------------------------------------------------------------------------------------
If Alias_Bard.GetActorRef().IsInFaction(BardSingerNoAutostart)
     stop()
ElseIf IsSinging(Alias_Bard.GetActorRef())
     stop()
ElseIf Alias_Bard.GetActorRef().IsInFaction(BardSingerInstrumentalOnly)
     (BardSongs as BardSongsScript).PlaySongRequest(alias_Bard.GetActorRef(),"Instrumental")
Else
     (BardSongs as BardSongsScript).PlaySongRequest(alias_Bard.GetActorRef())
EndIf

stop()
;END CODE
EndFunction
;END FRAGMENT

;END FRAGMENT CODE - Do not edit anything between this and the begin comment

; CellLoadResearch (0.8.25). The Story Manager starts this quest (BardSongStarter) on every change of the
; player's location (CLOC), and PlaySongRequest stops every song before choosing a new one. Behind a load
; door nobody heard the song break off; through a portal the song is heard from the street, and walking in
; cut it two seconds later (docs/FINDINGS.md, run 0.8.24). A bard who is singing now goes on singing.
Bool Function IsSinging(Actor akBard)
	BardSongsScript songs = BardSongs as BardSongsScript
	If !akBard || !songs || songs.BardSongs_Bard.GetActorRef() != akBard
		Return False
	EndIf
	Return songs.BardSongsBallad01Scene.IsPlaying() || songs.BardSongsBallad01WithIntroScene.IsPlaying() \
		|| songs.BardSongsDrinkingSong01Scene.IsPlaying() || songs.BardSongsDrinkingSong01WithIntroScene.IsPlaying() \
		|| songs.BardSongsDrinkingSong02Scene.IsPlaying() || songs.BardSongsDrinkingSong02WithIntroScene.IsPlaying() \
		|| songs.BardSongsDrinkingSong03Scene.IsPlaying() || songs.BardSongsDrinkingSong03WithIntroScene.IsPlaying() \
		|| songs.BardSongsInstrumentalFlute01.IsPlaying() || songs.BardSongsInstrumentalFlute02.IsPlaying() \
		|| songs.BardSongsInstrumentalLute01.IsPlaying() || songs.BardSongsInstrumentalLute02.IsPlaying() \
		|| songs.BardSongsInstrumentalDrum01.IsPlaying() || songs.BardSongsInstrumentalDrum02.IsPlaying() \
		|| songs.BardSongsInstrumentalFluteonly01.IsPlaying() || songs.BardSongsInstrumentalFluteonly02.IsPlaying() \
		|| songs.BardSongsInstrumentalWedding01.IsPlaying() || songs.BardSongsInstrumentalWedding02.IsPlaying()
EndFunction

Quest Property BardSongs  Auto

Keyword Property BardSongsKeyword  Auto

ReferenceAlias Property BardSongs_Bard  Auto

Faction Property BardSingerNoAutostart  Auto

Quest Property DA16  Auto

Faction Property BardSingerInstrumentalOnly  Auto

Package Property MorthalLurbukSleep1x5  Auto
