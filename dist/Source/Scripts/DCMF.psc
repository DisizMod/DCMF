Scriptname DCMF Hidden

; Feeds: values a script gives DCMF. A channel can be driven by a feed and a condition can read it.
; Register a feed each session (in OnInit and OnPlayerLoadGame), then set its values when they change.
; DCMF keeps the last value set for each actor and sets it again when the actor comes back into range.

; Registers a number feed, or a form feed with abForm, for each actor or, with abPerActor false, one for every actor.
; True once it is registered, also when this script registered it before; false when the key is taken or malformed
; (letters, digits, '.', '_' and '-' only).
bool Function RegisterFeed(string asKey, bool abForm = false, bool abPerActor = true, string asDisplayName = "", string asDescription = "") global native

; Sets a number feed for the actor; None for a feed for every actor. False when no number feed has the key.
bool Function SetFeedNumber(string asKey, Actor akActor, float afValue) global native

; Sets a form feed for the actor; None for a feed for every actor. False when no form feed has the key.
bool Function SetFeedForm(string asKey, Actor akActor, Form akValue) global native
