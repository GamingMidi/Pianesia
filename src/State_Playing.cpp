
// Copyright (c)2007 Nicholas Piegdon
// See license.txt for license information

#include "State_Playing.h"
#include "State_TrackSelection.h"
#include "State_Stats.h"
#include "Renderer.h"
#include "Textures.h"
#include "CompatibleSystem.h"

#include <string>
#include <iomanip>
using namespace std;

#include "string_util.h"
#include "MenuLayout.h"
#include "TextWriter.h"

#include "libmidi/Midi.h"
#include "libmidi/MidiTrack.h"
#include "libmidi/MidiEvent.h"
#include "libmidi/MidiUtil.h"

#include "libmidi/MidiComm.h"

void PlayingState::SetupNoteState()
{
    // Use swap to avoid multiple allocations
    TranslatedNoteSet old;
    old.swap(m_notes);

    for (const auto& n : old)
    {
        TranslatedNote note = n; // Copy once
        note.state = (m_state.track_properties[n.track_id].mode == Track::ModeYouPlay) ? UserPlayable : AutoPlayed;

        m_notes.insert(std::move(note)); // Use std::move to avoid unnecessary copying
    }
}


void PlayingState::ResetSong()
{
   if (m_state.midi_out) m_state.midi_out->Reset();
   if (m_state.midi_in) m_state.midi_in->Reset();

   // TODO: These should be moved to a configuration file
   // along with ALL other "const static something" variables.
   const static microseconds_t LeadIn = 5500000;
   const static microseconds_t LeadOut = 1000000;

   if (!m_state.midi) return;

   m_state.midi->Reset(LeadIn, LeadOut);

   m_notes = m_state.midi->Notes();
   SetupNoteState();

   m_state.stats = SongStatistics();
   m_state.stats.total_note_count = static_cast<int>(m_notes.size());

   m_current_combo = 0;

   m_note_offset = 0;
   m_max_allowed_title_alpha = 1.0;
}

PlayingState::PlayingState(const SharedState &state)
   : m_state(state), m_keyboard(0), m_first_update(true), m_paused(false), m_any_you_play_tracks(false)
{ }

void PlayingState::Init()
{
   if (!m_state.midi) throw GameStateError("PlayingState: Init was passed a null MIDI!");

   m_look_ahead_you_play_note_count = 0;
   for (size_t i = 0; i < m_state.track_properties.size(); ++i)
   {
      if (m_state.track_properties[i].mode == Track::ModeYouPlay)
      {
         m_look_ahead_you_play_note_count += m_state.midi->Tracks()[i].Notes().size();
         m_any_you_play_tracks = true;
      }
   }

   // This many microseconds of the song will
   // be shown on the screen at once
   const static microseconds_t DefaultShowDurationMicroseconds = 3250000;
   m_show_duration = DefaultShowDurationMicroseconds;

   m_keyboard = new KeyboardDisplay(KeyboardSize88, GetStateWidth() - Layout::ScreenMarginX*2, CalcKeyboardHeight());

   // Hide the mouse cursor while we're playing
   Compatible::HideMouseCursor();

   ResetSong();
}

PlayingState::~PlayingState()
{
   Compatible::ShowMouseCursor();
}

int PlayingState::CalcKeyboardHeight() const
{
   // Start with the size of the screen
   int height = GetStateHeight();

   // Allow a couple lines of text below the keys
   height -= Layout::ButtonFontSize * 8;

   return height;
}

void PlayingState::Play(microseconds_t delta_microseconds)
{
    MidiEventListWithTrackId evs = m_state.midi->Update(delta_microseconds);

    for (const auto& ev_pair : evs)
    {
        const size_t track_id = ev_pair.first;
        const MidiEvent& ev = ev_pair.second;

        bool draw = false;
        bool play = false;
        switch (m_state.track_properties[track_id].mode)
        {
        case Track::ModePlayedButHidden:
            play = true;
            break;
        case Track::ModePlayedAutomatically:
            draw = true;
            play = true;
            break;
        }

        if (draw && (ev.Type() == MidiEventType_NoteOn || ev.Type() == MidiEventType_NoteOff))
        {
            int vel = ev.NoteVelocity();
            const string name = MidiEvent::NoteName(ev.NoteNumber());

            m_keyboard->SetKeyActive(name, (vel > 0), m_state.track_properties[track_id].color);
        }

        if (play && m_state.midi_out)
            m_state.midi_out->Write(ev);
    }
}


double PlayingState::CalculateScoreMultiplier() const
{
   const static double MaxMultiplier = 5.0;
   double multiplier = 1.0;

   const double combo_addition = m_current_combo / 10.0;
   multiplier += combo_addition;

   return std::min(MaxMultiplier, multiplier);
}

void PlayingState::Listen()
{

}

void PlayingState::Update()
{
    // Calculate how visible the title bar should be
    constexpr double fade_in_ms = 350.0;
    constexpr double stay_ms = 2500.0;
    constexpr double fade_ms = 500.0;

    m_title_alpha = 0.0;
    unsigned long ms = GetStateMilliseconds() * std::max(m_state.song_speed, 50) / 100;

    if (ms <= stay_ms) {
        m_title_alpha = std::min(1.0, ms / fade_in_ms);
    }
    else {
        m_title_alpha = std::min(std::max((fade_ms - (ms - stay_ms)) / fade_ms, 0.0), 1.0);
        m_max_allowed_title_alpha = m_title_alpha;
    }
    m_title_alpha = std::min(m_title_alpha, m_max_allowed_title_alpha);

    microseconds_t delta_microseconds = static_cast<microseconds_t>(GetDeltaMilliseconds()) * 1000;
    delta_microseconds = (delta_microseconds / 100) * m_state.song_speed;

    if (m_paused) {
        delta_microseconds = 0;
    }

    if (!m_first_update) {
        Play(delta_microseconds);
    }
    m_first_update = false;

    microseconds_t cur_time = m_state.midi->GetSongPositionInMicroseconds();

    // Delete notes that are finished playing (and are no longer available to hit)
    auto i = m_notes.begin();
    while (i != m_notes.end()) {
        auto note = i++;
        microseconds_t window_end = note->start + (KeyboardDisplay::NoteWindowLength / 2);

        if (m_state.midi_in && note->state == UserPlayable && window_end <= cur_time) {
            TranslatedNote note_copy = *note;
            note_copy.state = UserMissed;
            m_notes.erase(note);
            note = m_notes.insert(note_copy).first;
        }

        if (note->start > cur_time) break;

        if (note->end < cur_time && window_end < cur_time) {
            if (note->state == UserMissed) {
                m_current_combo = 0;
                m_state.stats.notes_user_could_have_played++;
                m_state.stats.speed_integral += m_state.song_speed;
            }
            m_notes.erase(note);
        }
    }

    if (IsKeyPressed(KeyPlus)) {
        m_note_offset += 12;
    }

    if (IsKeyPressed(KeyMinus)) {
        m_note_offset -= 12;
    }

    if (IsKeyPressed(KeyUp)) {
        constexpr microseconds_t MinShowDuration = 2500;
        m_show_duration = std::max(m_show_duration - 25000, MinShowDuration);
    }

    if (IsKeyPressed(KeyDown)) {
        constexpr microseconds_t MaxShowDuration = 10000000;
        m_show_duration = std::min(m_show_duration + 25000, MaxShowDuration);
    }

    if (IsKeyPressed(KeyLeft)) {
        m_state.song_speed = std::max(m_state.song_speed - 10, 0);
    }

    if (IsKeyPressed(KeyRight)) {
        m_state.song_speed = std::min(m_state.song_speed + 10, 400);
    }

    if (IsKeyPressed(KeySpace)) {
        m_paused = !m_paused;
    }

    if (IsKeyPressed(KeyEscape)) {
        if (m_state.midi_out) m_state.midi_out->Reset();
        if (m_state.midi_in) m_state.midi_in->Reset();
        ChangeState(new TrackSelectionState(m_state));
        return;
    }

    if (m_state.midi->IsSongOver()) {
        if (m_state.midi_out) m_state.midi_out->Reset();
        if (m_state.midi_in) m_state.midi_in->Reset();

        if (m_state.midi_in && m_any_you_play_tracks) {
            ChangeState(new StatsState(m_state));
        }
        else {
            ChangeState(new TrackSelectionState(m_state));
        }
        return;
    }
}


void PlayingState::Draw(Renderer& renderer) const
{
    // Fetch textures outside the loop
    const Tga* key_tex[3] = {
        GetTexture(PlayKeyRail),
        GetTexture(PlayKeyShadow),
        GetTexture(PlayKeysBlack)
    };

    const Tga* note_tex[4] = {
        GetTexture(PlayNotesWhiteShadow, true),
        GetTexture(PlayNotesBlackShadow, true),
        GetTexture(PlayNotesWhiteColor, true),
        GetTexture(PlayNotesBlackColor, true)
    };

    renderer.ForceTexture(0);

    // Draw keyboard and notes
    m_keyboard->Draw(renderer, key_tex, note_tex, Layout::ScreenMarginX, 0, m_notes, m_show_duration,
        m_state.midi->GetSongPositionInMicroseconds(), m_state.track_properties);

    // Title and pause overlay
    double alpha = m_paused ? 1.0 : m_title_alpha;
    wstring title_text = m_paused ? L"Game Paused" : m_state.song_title;

    if (alpha > 0.001) {
        int quad_alpha = static_cast<int>(alpha * 160);
        renderer.SetColor(0, 0, 0, quad_alpha);
        renderer.DrawQuad(0, GetStateHeight() / 3, GetStateWidth(), 80);

        const Color c = Renderer::ToColor(255, 255, 255, static_cast<int>(alpha * 0xFF));
        TextWriter title(GetStateWidth() / 2, GetStateHeight() / 3 + 25, renderer, true, 24);
        title << Text(title_text, c);

        const Tga* keys = GetTexture(PlayKeys);
        renderer.SetColor(c);
        renderer.DrawTga(keys, GetStateWidth() / 2 - 250, GetStateHeight() / 2);
    }

    // Status display
    int text_y = CalcKeyboardHeight() + 42;
    renderer.SetColor(White);
    renderer.DrawTga(GetTexture(PlayStatus), Layout::ScreenMarginX - 1, text_y);
    renderer.DrawTga(GetTexture(PlayStatus2), Layout::ScreenMarginX + 273, text_y);

    // Score, multiplier, and speed display
    TextWriter score(Layout::ScreenMarginX + 92, text_y + 3, renderer, false, Layout::ScoreFontSize);
    score << static_cast<int>(m_state.stats.score);

    TextWriter multipliers(Layout::ScreenMarginX + 236, text_y + 9, renderer, false, Layout::TitleFontSize);
    multipliers << Text(WSTRING(fixed << setprecision(1) << CalculateScoreMultiplier()), Renderer::ToColor(138, 226, 52));

    int speed_x_offset = (m_state.song_speed >= 100 ? 0 : 11);
    TextWriter speed(Layout::ScreenMarginX + 413 + speed_x_offset, text_y + 9, renderer, false, Layout::TitleFontSize);
    speed << Text(WSTRING(m_state.song_speed << "%"), Renderer::ToColor(114, 159, 207));

    // Time display
    double non_zero_playback_speed = (m_state.song_speed == 0) ? 0.1 : (m_state.song_speed / 100.0);
    microseconds_t tot_seconds = static_cast<microseconds_t>((m_state.midi->GetSongLengthInMicroseconds() / 100000.0) / non_zero_playback_speed);
    microseconds_t cur_seconds = static_cast<microseconds_t>((m_state.midi->GetSongPositionInMicroseconds() / 100000.0) / non_zero_playback_speed);
    if (cur_seconds < 0) cur_seconds = 0;


    int completion = static_cast<int>(m_state.midi->GetSongPercentageComplete() * 100.0);
    unsigned int tot_min = static_cast<unsigned int>((tot_seconds / 10) / 60);
    unsigned int tot_sec = static_cast<unsigned int>((tot_seconds / 10) % 60);
    unsigned int tot_ten = static_cast<unsigned int>(tot_seconds % 10);
    const wstring total_time = WSTRING(tot_min << L":" << setfill(L'0') << setw(2) << tot_sec << L"." << tot_ten);

    unsigned int cur_min = static_cast<unsigned int>((cur_seconds / 10) / 60);
    unsigned int cur_sec = static_cast<unsigned int>((cur_seconds / 10) % 60);
    unsigned int cur_ten = static_cast<unsigned int>(cur_seconds % 10);
    const wstring current_time = WSTRING(cur_min << L":" << setfill(L'0') << setw(2) << cur_sec << L"." << cur_ten);
    const wstring percent_complete = WSTRING(L" (" << completion << L"%)");

    text_y += 30 + Layout::SmallFontSize;
    TextWriter time_text(Layout::ScreenMarginX + 39, text_y, renderer, false, Layout::SmallFontSize);
    time_text << WSTRING(current_time << L" / " << total_time << percent_complete);

    // Progress bars
    const int time_pb_width = static_cast<int>(m_state.midi->GetSongPercentageComplete() * (GetStateWidth() - Layout::ScreenMarginX * 2));
    const int pb_x = Layout::ScreenMarginX;
    const int pb_y = CalcKeyboardHeight() + 25;

    renderer.SetColor(0x50, 0x50, 0x50);
    renderer.DrawQuad(pb_x, pb_y, time_pb_width, 16);

    if (m_look_ahead_you_play_note_count > 0) {
        const double note_count = static_cast<double>(m_look_ahead_you_play_note_count);

        const int note_miss_pb_width = static_cast<int>(m_state.stats.notes_user_could_have_played / note_count * (GetStateWidth() - Layout::ScreenMarginX * 2));
        const int note_hit_pb_width = static_cast<int>(m_state.stats.notes_user_actually_played / note_count * (GetStateWidth() - Layout::ScreenMarginX * 2));

        renderer.SetColor(0xCE, 0x5C, 0x00);
        renderer.DrawQuad(pb_x, pb_y - 20, note_miss_pb_width, 16);

        renderer.SetColor(0xFC, 0xAF, 0x3E);
        renderer.DrawQuad(pb_x, pb_y - 20, note_hit_pb_width, 16);
    }

    // Combo display
    if (m_current_combo > 5) {
        int combo_font_size = 20 + (m_current_combo / 10);
        int combo_x = GetStateWidth() / 2;
        int combo_y = GetStateHeight() - CalcKeyboardHeight() + 30 - (combo_font_size / 2);

        TextWriter combo_text(combo_x, combo_y, renderer, true, combo_font_size);
        combo_text << WSTRING(m_current_combo << L" Combo!");
    }
}