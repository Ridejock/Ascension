// Title, overwrite, and character create. No window. The name rules and the
// menu steps are the checks. Drawing stays in the app.

#include <string>

#include "Ascension/Progress/FrontDoor.h"
#include "Check.h"

namespace {

using namespace Ascension;

void TestHeroName()
{
    Check(kHeroNameMin == 1 && kHeroNameMax == 16, "a name is 1 to 16 characters");
    const HeroNameCheck spaced = CheckHeroName("  Mara   Stone ");
    Check(spaced.Status == HeroNameStatus::Ok && spaced.Name == "Mara Stone",
          "ends trim and a double space collapses");
    const HeroNameCheck mark = CheckHeroName("O'Neil-2");
    Check(mark.Status == HeroNameStatus::Ok && mark.Name == "O'Neil-2",
          "a letter, a digit, a hyphen, and an apostrophe stay");
    Check(CheckHeroName("   ").Status == HeroNameStatus::Empty, "a blank name is empty");
    Check(CheckHeroName("").Status == HeroNameStatus::Empty, "an empty name is refused");
    Check(CheckHeroName("Mara!").Status == HeroNameStatus::BadChar, "a mark outside the set is refused");
    const std::string longName(17, 'A');
    Check(CheckHeroName(longName).Status == HeroNameStatus::TooLong, "17 characters is too long");
    Check(CheckHeroName(std::string(16, 'B')).Status == HeroNameStatus::Ok,
          "16 characters is the last that fits");

    std::string draft;
    Check(AppendHeroName(draft, "A") == NameEdit::Added && draft == "A", "a letter is kept");
    Check(AppendHeroName(draft, "  ") == NameEdit::Added && draft == "A ",
          "one space is kept so the next word can follow");
    Check(AppendHeroName(draft, " ") == NameEdit::Kept && draft == "A ", "a second space collapses");
    Check(AppendHeroName(draft, "B") == NameEdit::Added && draft == "A B", "the next word joins");
    Check(AppendHeroName(draft, "!") == NameEdit::Rejected && draft == "A B",
          "a bad character does not change the draft");
    draft.assign(16, 'C');
    Check(AppendHeroName(draft, "D") == NameEdit::Full && draft.size() == 16,
          "the 17th character is not added");
    PopHeroName(draft);
    Check(draft.size() == 15, "backspace drops the last character");
}

void TestTitleFlow()
{
    FrontDoor door;
    Check(ApplyFrontAction(door, kConfirm, 4) == FrontStep::None && door.Phase == FrontPhase::Title,
          "continue does nothing when there is no save");
    Check(ApplyFrontAction(door, kPause, 4) == FrontStep::Quit && door.Phase == FrontPhase::Title,
          "escape quits from the title");
    door.Row = kTitleRowNew;
    Check(ApplyFrontAction(door, kConfirm, 4) == FrontStep::None && door.Phase == FrontPhase::Name,
          "new game with no save opens the name");
    door.Name = "  Mara ";
    Check(ApplyFrontAction(door, kConfirm, 4) == FrontStep::None && door.Phase == FrontPhase::Class &&
              door.Name == "Mara",
          "a trimmed name opens the class list");
    Check(ApplyFrontAction(door, kMoveSouth, 4) == FrontStep::None && door.Row == 1,
          "south marks the next starter");
    Check(ApplyFrontAction(door, kConfirm, 4) == FrontStep::None && door.Phase == FrontPhase::Begin &&
              door.ClassRow == 1,
          "confirm on a class opens begin");
    Check(ApplyFrontAction(door, kCancel, 4) == FrontStep::None && door.Phase == FrontPhase::Class &&
              door.Row == 1,
          "cancel from begin returns to that class");
    Check(ApplyFrontAction(door, kConfirm, 4) == FrontStep::None && door.Phase == FrontPhase::Begin,
          "confirm returns to begin");
    Check(ApplyFrontAction(door, kConfirm, 4) == FrontStep::Began && door.Name == "Mara" &&
              door.ClassRow == 1,
          "begin is the created hero");

    FrontDoor saved;
    saved.SaveExists = true;
    saved.Row = kTitleRowNew;
    Check(ApplyFrontAction(saved, kConfirm, 4) == FrontStep::None &&
              saved.Phase == FrontPhase::Overwrite,
          "new game over a save asks first");
    Check(ApplyFrontAction(saved, kCancel, 4) == FrontStep::None &&
              saved.Phase == FrontPhase::Title && saved.Row == kTitleRowNew,
          "cancel leaves the save in place");
    saved.Row = kTitleRowContinue;
    Check(ApplyFrontAction(saved, kConfirm, 4) == FrontStep::Continue,
          "continue loads the save");
    saved.Phase = FrontPhase::Title;
    saved.Row = kTitleRowNew;
    Check(ApplyFrontAction(saved, kConfirm, 4) == FrontStep::None, "new game asks again");
    Check(ApplyFrontAction(saved, kConfirm, 4) == FrontStep::None && saved.Phase == FrontPhase::Name,
          "overwrite opens the name");

    FrontDoor blank;
    blank.Phase = FrontPhase::Name;
    blank.Return = FrontPhase::Title;
    Check(ApplyFrontAction(blank, kConfirm, 4) == FrontStep::None &&
              blank.Notice == FrontNotice::Empty && blank.Phase == FrontPhase::Name,
          "an empty name stays on the name step");
    blank.Name = "Mara!";
    Check(ApplyFrontAction(blank, kConfirm, 4) == FrontStep::None &&
              blank.Notice == FrontNotice::BadChar,
          "a refused name stays on the name step");
    blank.Name = "Ada";
    Check(ApplyFrontAction(blank, kCancel, 4) == FrontStep::None && blank.Name == "Ad" &&
              blank.Phase == FrontPhase::Name,
          "cancel deletes one character");
    Check(ApplyFrontAction(blank, kPause, 4) == FrontStep::None && blank.Phase == FrontPhase::Title,
          "escape leaves create and does not quit");

    FrontDoor debug;
    debug.Phase = FrontPhase::Playing;
    OpenDebugCreate(debug);
    Check(debug.Phase == FrontPhase::Name && debug.Debug && debug.Return == FrontPhase::Playing,
          "F9 opens create over the running game");
    debug.Name = "Bo";
    Check(ApplyFrontAction(debug, kPause, 4) == FrontStep::None &&
              debug.Phase == FrontPhase::Playing && !debug.Debug,
          "escape from that create returns to the game");
}

} // namespace

void TestFrontDoor()
{
    TestHeroName();
    TestTitleFlow();
}
