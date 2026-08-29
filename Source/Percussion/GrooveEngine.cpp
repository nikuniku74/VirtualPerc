#include "Percussion/GrooveEngine.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace vp
{

namespace
{
    // Everything is on a sixteenth grid. Steps 0, 4, 8, 12 are the quarters;
    // 2, 6, 10, 14 are the off-eighths; the odd steps are the sixteenths in
    // between - the "e" and the "a" - where the quiet strokes live.
    struct Hit { int step; Stroke stroke; float velocity; };

    // ---------------------------------------------------------------- marcha
    //
    // What makes this a marcha and not a list of conga hits is the last two
    // entries: open, open on 4 and the "and" of 4, together, pulling into the
    // next bar. Everything before them is there to leave room for that.
    //
    // The heel-toe pair that opens the bar starts on the "e" of 1 rather than
    // on the one: the band has the downbeat, and a muffled stroke underneath it
    // is inaudible anyway.
    constexpr Hit kMarchaA[] = {
        {  1, Stroke::heel,  0.34f },
        {  2, Stroke::toe,   0.30f },
        {  4, Stroke::slap,  0.94f },   // the crack on 2
        {  6, Stroke::toe,   0.28f },
        {  8, Stroke::tumba, 0.82f },   // the bass on 3
        { 10, Stroke::toe,   0.28f },
        { 12, Stroke::open,  0.86f },   // and the pair
        { 14, Stroke::open,  0.92f },
    };
    constexpr Hit kMarchaB[] = {
        {  1, Stroke::heel,  0.32f },
        {  2, Stroke::toe,   0.29f },
        {  4, Stroke::slap,  0.92f },
        {  6, Stroke::heel,  0.26f },
        {  8, Stroke::tumba, 0.80f },
        { 10, Stroke::tumba, 0.52f },
        { 12, Stroke::open,  0.84f },
        { 14, Stroke::open,  0.94f },
    };
    // The third bar of the phrase. Two bars of A and B alternating is a loop
    // you hear the seam of after twenty seconds; four bars is a sentence. C is
    // the one that goes somewhere - here the tumba answers itself across the
    // second half - and the phrase reads A B A C, so the change lands where a
    // listener is already expecting one.
    constexpr Hit kMarchaC[] = {
        {  1, Stroke::heel,  0.32f },
        {  2, Stroke::toe,   0.28f },
        {  4, Stroke::slap,  0.90f },
        {  6, Stroke::toe,   0.30f },
        {  8, Stroke::tumba, 0.78f },
        { 11, Stroke::tumba, 0.46f },
        { 12, Stroke::open,  0.88f },
        { 14, Stroke::open,  0.95f },
    };
    constexpr Hit kMarchaFill[] = {
        {  8, Stroke::open,  0.72f },
        { 10, Stroke::slap,  0.80f },
        { 11, Stroke::open,  0.62f },
        { 12, Stroke::slap,  0.88f },
        { 13, Stroke::open,  0.70f },
        { 14, Stroke::open,  0.95f },
        { 15, Stroke::slap,  0.78f },
    };
    // Fourth bar of the eight-bar sentence. The opens still close the bar -
    // that is the marcha - but the tumba answers early, so this is a different
    // riff rather than C played again.
    constexpr Hit kMarchaD[] = {
        {  1, Stroke::heel,  0.30f },
        {  2, Stroke::toe,   0.28f },
        {  4, Stroke::slap,  0.88f },
        {  5, Stroke::heel,  0.22f },
        {  8, Stroke::tumba, 0.74f },
        { 10, Stroke::open,  0.50f },
        { 12, Stroke::open,  0.82f },
        { 14, Stroke::open,  0.94f },
    };

    // ------------------------------------------------------------------ rock
    //
    // A percussionist in a rock band is not the drummer twice. The snare owns
    // 2 and 4, so the congas stay off them and play the gaps: the low drum
    // anchors the one, and the open tones land on the "and" of 2 and the "and"
    // of 4, the second of which is what pushes into the next bar. Doubling the
    // backbeat is the commonest way to make a rock track sound crowded.
    constexpr Hit kRockA[] = {
        {  2, Stroke::tumba, 0.84f },   // the "and" of 1, never the one itself
        {  3, Stroke::heel,  0.24f },
        {  6, Stroke::open,  0.74f },   // and of 2
        {  8, Stroke::tumba, 0.60f },
        { 11, Stroke::toe,   0.24f },
        { 14, Stroke::open,  0.90f },   // and of 4 - the push
    };
    constexpr Hit kRockB[] = {
        {  2, Stroke::tumba, 0.82f },
        {  4, Stroke::slap,  0.58f },   // a light answer on the backbeat
        {  6, Stroke::open,  0.72f },
        {  8, Stroke::tumba, 0.58f },
        { 11, Stroke::toe,   0.26f },
        { 13, Stroke::open,  0.52f },
        { 14, Stroke::open,  0.92f },
    };
    // Still nothing on 2 and 4 - that is the drummer's - but the bar leaves with
    // a pickup on the last sixteenth.
    constexpr Hit kRockC[] = {
        {  2, Stroke::tumba, 0.86f },
        {  3, Stroke::heel,  0.22f },
        {  6, Stroke::open,  0.76f },
        {  8, Stroke::tumba, 0.62f },
        { 10, Stroke::toe,   0.26f },
        { 14, Stroke::open,  0.88f },
        { 15, Stroke::slap,  0.58f },
    };
    constexpr Hit kRockFill[] = {
        { 12, Stroke::open,  0.72f },
        { 13, Stroke::open,  0.80f },
        { 14, Stroke::slap,  0.88f },
        { 15, Stroke::open,  0.94f },
    };
    // Still off 2 and 4. A slap on the "a" of 2 is a pickup, not a backbeat.
    constexpr Hit kRockD[] = {
        {  1, Stroke::heel,  0.22f },
        {  2, Stroke::tumba, 0.84f },
        {  6, Stroke::open,  0.70f },
        {  7, Stroke::slap,  0.42f },
        {  8, Stroke::tumba, 0.58f },
        { 11, Stroke::toe,   0.24f },
        { 14, Stroke::open,  0.88f },
    };

    // ----------------------------------------------------------------- dance
    //
    // A tumbao stated, not a tumbao filled in. The posts are the same ones the
    // salsa loop this was transcribed from puts down - the low drum answering
    // the one from its "e", nothing at all on 2, the slap as the loudest
    // stroke on the "e" of 2, and the open tones closing the bar into the next
    // - and everything that was only joining them up is gone.
    //
    // It used to be thirteen strokes a bar, three and four to a quarter. That
    // is a percussion loop, and under a band it is a wall: the whole point of
    // a conga part is the space between the strokes, because that is where the
    // rest of the band is. Six a bar says the same figure and leaves the room.
    constexpr Hit kDanceA[] = {
        {  1, Stroke::tumba, 0.90f },   // the one answered, not doubled
        {  5, Stroke::slap,  0.94f },   // the "e" of 2 - the loudest stroke
        {  8, Stroke::tumba, 0.88f },   // the three
        { 11, Stroke::open,  0.80f },
        { 14, Stroke::open,  0.78f },   // and the pair into the next bar
        { 15, Stroke::open,  0.86f },
    };
    constexpr Hit kDanceB[] = {
        {  1, Stroke::tumba, 0.86f },
        {  5, Stroke::slap,  0.92f },
        {  7, Stroke::open,  0.74f },   // the answer sits on the "a" of 2
        {  8, Stroke::tumba, 0.86f },
        { 14, Stroke::open,  0.80f },
    };
    constexpr Hit kDanceC[] = {
        {  1, Stroke::tumba, 0.88f },
        {  5, Stroke::slap,  0.96f },
        {  6, Stroke::open,  0.70f },
        { 10, Stroke::open,  0.74f },   // the bar that goes somewhere: the
        { 14, Stroke::open,  0.76f },   // opens walk up instead of repeating
        { 15, Stroke::open,  0.88f },
    };
    // A fill is the one bar that may be busier than the rule allows - it is
    // what a fill is - but eight straight sixteenths is a drum machine. Six,
    // with a gap in them, is a player.
    constexpr Hit kDanceFill[] = {
        {  8, Stroke::slap,  0.70f },
        { 10, Stroke::open,  0.74f },
        { 11, Stroke::slap,  0.68f },
        { 13, Stroke::open,  0.82f },
        { 14, Stroke::slap,  0.88f },
        { 15, Stroke::open,  0.96f },
    };
    constexpr Hit kDanceD[] = {
        {  2, Stroke::open,  0.84f },   // no low drum at all in this one
        {  5, Stroke::slap,  0.90f },
        {  8, Stroke::tumba, 0.86f },
        { 11, Stroke::open,  0.82f },
        { 15, Stroke::open,  0.88f },
    };

    // ------------------------------------------------------------------- pop
    //
    // The job here is to be felt and not noticed. There is a vocal in the
    // middle of the record, so the part is mostly space: the one, a light lift
    // into 3, and the push into the next bar.
    constexpr Hit kPopA[] = {
        {  2, Stroke::tumba, 0.72f },
        {  7, Stroke::toe,   0.30f },
        { 14, Stroke::open,  0.80f },
    };
    constexpr Hit kPopB[] = {
        {  2, Stroke::tumba, 0.70f },
        {  8, Stroke::open,  0.56f },
        { 11, Stroke::toe,   0.28f },
        { 14, Stroke::open,  0.82f },
    };
    // Even the variation stays out of the way.
    constexpr Hit kPopC[] = {
        {  2, Stroke::tumba, 0.74f },
        {  6, Stroke::toe,   0.26f },
        { 11, Stroke::open,  0.52f },
        { 14, Stroke::open,  0.84f },
    };
    constexpr Hit kPopFill[] = {
        { 10, Stroke::toe,   0.40f },
        { 12, Stroke::open,  0.66f },
        { 14, Stroke::open,  0.86f },
        { 15, Stroke::slap,  0.62f },
    };
    // Still mostly space. A muffled tone on 3 is as busy as this part gets.
    constexpr Hit kPopD[] = {
        {  2, Stroke::tumba, 0.72f },
        {  4, Stroke::heel,  0.22f },
        {  8, Stroke::muff,  0.36f },
        { 14, Stroke::open,  0.82f },
        { 15, Stroke::toe,   0.30f },
    };

    // ----------------------------------------------------------------- samba
    //
    // Brazilian weight sits on 2 and 4, not on the tumbao's 3. The opens are
    // syncopated around those two posts rather than paired at the end of the
    // bar, which is the whole difference between this and a marcha.
    constexpr Hit kSambaA[] = {
        {  1, Stroke::heel,  0.28f },
        {  2, Stroke::toe,   0.32f },
        {  4, Stroke::tumba, 0.90f },   // samba 2
        {  7, Stroke::slap,  0.52f },
        { 10, Stroke::toe,   0.28f },
        { 12, Stroke::tumba, 0.86f },   // samba 4
        { 14, Stroke::open,  0.78f },
    };
    constexpr Hit kSambaB[] = {
        {  1, Stroke::heel,  0.26f },
        {  3, Stroke::open,  0.52f },
        {  4, Stroke::tumba, 0.88f },
        {  6, Stroke::open,  0.66f },
        {  8, Stroke::heel,  0.24f },
        { 11, Stroke::slap,  0.50f },
        { 12, Stroke::tumba, 0.84f },
        { 14, Stroke::open,  0.82f },
    };
    constexpr Hit kSambaC[] = {
        {  1, Stroke::toe,   0.26f },
        {  4, Stroke::tumba, 0.92f },
        {  6, Stroke::open,  0.72f },
        {  9, Stroke::heel,  0.24f },
        { 12, Stroke::tumba, 0.80f },
        { 14, Stroke::open,  0.84f },
    };
    constexpr Hit kSambaD[] = {
        {  1, Stroke::tumba, 0.46f },
        {  2, Stroke::toe,   0.30f },
        {  4, Stroke::tumba, 0.86f },
        {  7, Stroke::open,  0.64f },
        { 10, Stroke::slap,  0.52f },
        { 12, Stroke::tumba, 0.88f },
        { 14, Stroke::open,  0.76f },
    };
    constexpr Hit kSambaFill[] = {
        {  8, Stroke::open,  0.62f },
        {  9, Stroke::slap,  0.70f },
        { 10, Stroke::open,  0.66f },
        { 11, Stroke::tumba, 0.74f },
        { 12, Stroke::open,  0.80f },
        { 13, Stroke::slap,  0.84f },
        { 14, Stroke::open,  0.90f },
        { 15, Stroke::tumba, 0.94f },
    };

    // ------------------------------------------------------------------ funk
    //
    // The snare still owns 2 and 4, so those stay empty of loud congas. What
    // makes it funk rather than rock is the sixteenth slaps *around* them and
    // the open that lands on the "a" of 4 - the one a horn section would hit.
    constexpr Hit kFunkA[] = {
        {  1, Stroke::tumba, 0.78f },
        {  3, Stroke::slap,  0.50f },   // the "a" of 1
        {  6, Stroke::open,  0.62f },
        { 10, Stroke::toe,   0.26f },
        { 11, Stroke::slap,  0.46f },
        { 14, Stroke::muff,  0.38f },
        { 15, Stroke::open,  0.88f },   // a of 4
    };
    constexpr Hit kFunkB[] = {
        {  1, Stroke::tumba, 0.74f },
        {  3, Stroke::slap,  0.48f },
        {  6, Stroke::open,  0.58f },
        {  7, Stroke::toe,   0.24f },
        { 11, Stroke::slap,  0.52f },
        { 15, Stroke::open,  0.90f },
    };
    constexpr Hit kFunkC[] = {
        {  1, Stroke::tumba, 0.80f },
        {  3, Stroke::slap,  0.54f },
        {  6, Stroke::open,  0.64f },
        {  9, Stroke::heel,  0.22f },
        { 11, Stroke::open,  0.50f },
        { 13, Stroke::slap,  0.44f },
        { 15, Stroke::open,  0.92f },
    };
    constexpr Hit kFunkD[] = {
        {  1, Stroke::tumba, 0.76f },
        {  3, Stroke::slap,  0.56f },
        {  5, Stroke::toe,   0.24f },
        {  6, Stroke::open,  0.60f },
        { 10, Stroke::muff,  0.34f },
        { 11, Stroke::slap,  0.48f },
        { 14, Stroke::open,  0.52f },
        { 15, Stroke::open,  0.86f },
    };
    constexpr Hit kFunkFill[] = {
        {  8, Stroke::slap,  0.58f },
        {  9, Stroke::open,  0.54f },
        { 10, Stroke::slap,  0.66f },
        { 11, Stroke::open,  0.62f },
        { 12, Stroke::slap,  0.74f },
        { 13, Stroke::open,  0.78f },
        { 14, Stroke::slap,  0.84f },
        { 15, Stroke::open,  0.94f },
    };

    // ---------------------------------------------------------------- reggae
    //
    // One-drop: the one is empty. The three is the post. The shaker skanks on
    // the offbeat the way a guitar would, and the congas stay out of the way
    // of both.
    constexpr Hit kReggaeA[] = {
        {  2, Stroke::open,  0.58f },
        {  6, Stroke::toe,   0.28f },
        {  8, Stroke::tumba, 0.90f },   // the three
        { 10, Stroke::open,  0.64f },
        { 14, Stroke::slap,  0.50f },
    };
    constexpr Hit kReggaeB[] = {
        {  2, Stroke::open,  0.54f },
        {  4, Stroke::heel,  0.22f },
        {  6, Stroke::toe,   0.26f },
        {  8, Stroke::tumba, 0.86f },
        { 10, Stroke::open,  0.68f },
        { 14, Stroke::open,  0.56f },
    };
    constexpr Hit kReggaeC[] = {
        {  2, Stroke::open,  0.60f },
        {  6, Stroke::slap,  0.40f },
        {  8, Stroke::tumba, 0.88f },
        { 11, Stroke::toe,   0.26f },
        { 14, Stroke::open,  0.62f },
        { 15, Stroke::open,  0.48f },
    };
    constexpr Hit kReggaeD[] = {
        {  2, Stroke::open,  0.56f },
        {  3, Stroke::heel,  0.20f },
        {  8, Stroke::tumba, 0.84f },
        { 10, Stroke::open,  0.70f },
        { 12, Stroke::muff,  0.32f },
        { 14, Stroke::slap,  0.52f },
    };
    constexpr Hit kReggaeFill[] = {
        {  8, Stroke::tumba, 0.70f },
        { 10, Stroke::open,  0.62f },
        { 11, Stroke::slap,  0.58f },
        { 12, Stroke::open,  0.74f },
        { 13, Stroke::tumba, 0.66f },
        { 14, Stroke::open,  0.82f },
        { 15, Stroke::slap,  0.78f },
    };

    // ----------------------------------------------------------------- bossa
    //
    // The 3-2 bossa clave, stated as conga tones rather than as a stick: on 1,
    // the "a" of 1, the "and" of 2, the "and" of 3, the "and" of 4. Sparse
    // enough to sit under a vocal, clave-shaped enough not to be pop.
    constexpr Hit kBossaA[] = {
        {  2, Stroke::tumba, 0.70f },
        {  3, Stroke::open,  0.62f },
        {  6, Stroke::muff,  0.40f },
        { 10, Stroke::open,  0.68f },
        { 14, Stroke::open,  0.78f },
    };
    constexpr Hit kBossaB[] = {
        {  2, Stroke::tumba, 0.66f },
        {  3, Stroke::open,  0.58f },
        {  6, Stroke::toe,   0.28f },
        { 10, Stroke::open,  0.72f },
        { 12, Stroke::heel,  0.24f },
        { 14, Stroke::open,  0.80f },
    };
    constexpr Hit kBossaC[] = {
        {  2, Stroke::tumba, 0.72f },
        {  3, Stroke::open,  0.64f },
        {  7, Stroke::slap,  0.36f },
        { 10, Stroke::open,  0.66f },
        { 14, Stroke::open,  0.82f },
        { 15, Stroke::toe,   0.28f },
    };
    constexpr Hit kBossaD[] = {
        {  2, Stroke::tumba, 0.68f },
        {  3, Stroke::open,  0.60f },
        {  6, Stroke::muff,  0.38f },
        {  8, Stroke::heel,  0.22f },
        { 10, Stroke::open,  0.70f },
        { 14, Stroke::open,  0.76f },
    };
    constexpr Hit kBossaFill[] = {
        { 10, Stroke::open,  0.56f },
        { 12, Stroke::tumba, 0.62f },
        { 13, Stroke::open,  0.70f },
        { 14, Stroke::open,  0.82f },
        { 15, Stroke::slap,  0.66f },
    };

    // The shaker, as a velocity per sixteenth. Zero is silence. The subdivision
    // setting thins this down; it never adds to it.
    //
    // A shaker is two strokes - down on the pulse, up on the return - so which
    // one sounds follows from the step, not from the table. What the table
    // carries is where the weight goes, and that is different for every style:
    // latin leans on the pulse, rock leans on the backbeat with the drummer,
    // dance leans on the offbeat where the open hat sits, pop stays level and
    // out of the way.
    //
    // These used to be one beat of weights written out four times - the same
    // four numbers repeated - which is not a figure, it is a cell. However
    // musical the numbers were, four identical beats read as a machine, because
    // nothing in the bar told you where you were in it. Each table now states
    // its shape over two beats and answers it over the next two, so the phrase
    // is a bar long and the halves are not the same.
    struct StyleSpec
    {
        const Hit* barA; int nA;
        const Hit* barB; int nB;
        const Hit* barC; int nC;
        const Hit* barD; int nD;
        const Hit* fill; int nFill;
        float shaker[GrooveEngine::kStepsPerBar];
        // Where the weight sits across the bar. This belongs to the style and
        // not to the engine: latin and pop lean on the one, but a rock part
        // leans on 2 and 4 with the drummer, and a four-on-the-floor bar is
        // nearly even because the kick is on every beat. A single global
        // contour favouring beat one silently cancelled the backbeat emphasis
        // the rock pattern was asking for.
        float accent[4];
        float ghostChance;      // how much the player fills the gaps
        int   fillEveryBars;
    };

    constexpr StyleSpec kStyles[static_cast<int> (GrooveStyle::count)] = {
        // marcha
        { kMarchaA, static_cast<int> (std::size (kMarchaA)),
          kMarchaB, static_cast<int> (std::size (kMarchaB)),
          kMarchaC, static_cast<int> (std::size (kMarchaC)),
          kMarchaD, static_cast<int> (std::size (kMarchaD)),
          kMarchaFill, static_cast<int> (std::size (kMarchaFill)),
          { 0.90f, 0.36f, 0.54f, 0.36f,  0.74f, 0.36f, 0.63f, 0.42f,
            0.86f, 0.36f, 0.54f, 0.36f,  0.70f, 0.42f, 0.80f, 0.48f },
          { 1.00f, 0.86f, 0.93f, 0.88f },
          0.35f, 8 },

        // rock - the weight moves onto 2 and 4, with the drummer
        { kRockA, static_cast<int> (std::size (kRockA)),
          kRockB, static_cast<int> (std::size (kRockB)),
          kRockC, static_cast<int> (std::size (kRockC)),
          kRockD, static_cast<int> (std::size (kRockD)),
          kRockFill, static_cast<int> (std::size (kRockFill)),
          { 0.78f, 0.28f, 0.58f, 0.28f,  0.90f, 0.28f, 0.60f, 0.28f,
            0.76f, 0.28f, 0.64f, 0.32f,  0.96f, 0.32f, 0.72f, 0.38f },
          { 0.94f, 1.00f, 0.92f, 1.00f },
          0.18f, 8 },

        // dance - sixteenths, leaning on the offbeat like an open hat
        { kDanceA, static_cast<int> (std::size (kDanceA)),
          kDanceB, static_cast<int> (std::size (kDanceB)),
          kDanceC, static_cast<int> (std::size (kDanceC)),
          kDanceD, static_cast<int> (std::size (kDanceD)),
          kDanceFill, static_cast<int> (std::size (kDanceFill)),
          { 0.70f, 0.40f, 0.88f, 0.40f,  0.58f, 0.52f, 0.90f, 0.62f,
            0.72f, 0.38f, 0.86f, 0.46f,  0.54f, 0.60f, 0.94f, 0.74f },
          { 1.00f, 0.95f, 0.97f, 0.95f },
          0.30f, 8 },

        // pop - level, quiet, and mostly space
        { kPopA, static_cast<int> (std::size (kPopA)),
          kPopB, static_cast<int> (std::size (kPopB)),
          kPopC, static_cast<int> (std::size (kPopC)),
          kPopD, static_cast<int> (std::size (kPopD)),
          kPopFill, static_cast<int> (std::size (kPopFill)),
          { 0.74f, 0.22f, 0.50f, 0.22f,  0.60f, 0.24f, 0.54f, 0.26f,
            0.70f, 0.22f, 0.50f, 0.22f,  0.58f, 0.26f, 0.62f, 0.30f },
          { 1.00f, 0.88f, 0.94f, 0.90f },
          0.10f, 8 },

        // samba - sixteenths, weight on 2 and 4
        { kSambaA, static_cast<int> (std::size (kSambaA)),
          kSambaB, static_cast<int> (std::size (kSambaB)),
          kSambaC, static_cast<int> (std::size (kSambaC)),
          kSambaD, static_cast<int> (std::size (kSambaD)),
          kSambaFill, static_cast<int> (std::size (kSambaFill)),
          { 0.72f, 0.34f, 0.58f, 0.40f,  0.94f, 0.36f, 0.70f, 0.48f,
            0.68f, 0.32f, 0.56f, 0.38f,  0.90f, 0.44f, 0.76f, 0.58f },
          { 0.90f, 1.00f, 0.88f, 1.00f },
          0.28f, 8 },

        // funk - sixteenths, snappy, leaning into the "a"
        { kFunkA, static_cast<int> (std::size (kFunkA)),
          kFunkB, static_cast<int> (std::size (kFunkB)),
          kFunkC, static_cast<int> (std::size (kFunkC)),
          kFunkD, static_cast<int> (std::size (kFunkD)),
          kFunkFill, static_cast<int> (std::size (kFunkFill)),
          { 0.88f, 0.42f, 0.56f, 0.50f,  0.70f, 0.38f, 0.62f, 0.54f,
            0.80f, 0.36f, 0.58f, 0.48f,  0.64f, 0.52f, 0.60f, 0.78f },
          { 1.00f, 0.92f, 0.96f, 0.94f },
          0.45f, 8 },

        // reggae - offbeat skank, quieter on the pulse
        { kReggaeA, static_cast<int> (std::size (kReggaeA)),
          kReggaeB, static_cast<int> (std::size (kReggaeB)),
          kReggaeC, static_cast<int> (std::size (kReggaeC)),
          kReggaeD, static_cast<int> (std::size (kReggaeD)),
          kReggaeFill, static_cast<int> (std::size (kReggaeFill)),
          { 0.42f, 0.22f, 0.90f, 0.22f,  0.38f, 0.40f, 0.84f, 0.50f,
            0.48f, 0.22f, 0.92f, 0.28f,  0.36f, 0.32f, 0.80f, 0.62f },
          { 0.82f, 0.90f, 1.00f, 0.88f },
          0.08f, 8 },

        // bossa - level eighths, a little extra on the clave "a"
        { kBossaA, static_cast<int> (std::size (kBossaA)),
          kBossaB, static_cast<int> (std::size (kBossaB)),
          kBossaC, static_cast<int> (std::size (kBossaC)),
          kBossaD, static_cast<int> (std::size (kBossaD)),
          kBossaFill, static_cast<int> (std::size (kBossaFill)),
          { 0.70f, 0.24f, 0.52f, 0.36f,  0.58f, 0.22f, 0.56f, 0.28f,
            0.66f, 0.24f, 0.54f, 0.32f,  0.56f, 0.28f, 0.64f, 0.40f },
          { 1.00f, 0.88f, 0.92f, 0.90f },
          0.12f, 8 },
    };

    // A triplet-feel off-eighth sits two thirds of the way through the beat
    // instead of half way, which is a sixth of a beat late.
    constexpr float kFullSwingBeats = 1.0f / 6.0f;

    // Micro-timing. A player is never on the grid, but a percussionist is much
    // closer to it than this range would suggest at full humanize - which is
    // why the default is well under 1.
    constexpr float kFeelBiasBeatsAt120 = 0.004f;   // ~2 ms of natural lateness
    constexpr float kFeelSpreadBeatsAt120 = 0.006f; // ~3 ms either side

    int wrapBar (int barIndex, int period) noexcept
    {
        if (period <= 0)
            return 0;
        return ((barIndex % period) + period) % period;
    }
}

void GrooveEngine::prepare (std::uint32_t seed) noexcept
{
    rng.reset (seed);
    reset();
}

void GrooveEngine::reset() noexcept
{
}

void GrooveEngine::setStyle (GrooveStyle s) noexcept
{
    const int i = static_cast<int> (s);
    style = (i >= 0 && i < static_cast<int> (GrooveStyle::count)) ? s : GrooveStyle::marcha;
}

void GrooveEngine::setHumanize (float amount) noexcept
{
    humanize = clamp01 (amount);
}

void GrooveEngine::setSwing (float amount) noexcept
{
    swing = clamp01 (amount);
}

void GrooveEngine::setIntensity (float amount) noexcept
{
    intensity = clamp01 (amount);
}

void GrooveEngine::setShakerSubdivision (Subdivision s) noexcept
{
    shakerGrid = s == Subdivision::autoDetect ? Subdivision::eighth : s;
}

float GrooveEngine::humanVelocity (float base) noexcept
{
    const float spread = 0.20f * humanize;
    return std::clamp (base * (1.0f + spread * rng.nextSigned()), 0.05f, 1.0f);
}

float GrooveEngine::humanDelay (int step) noexcept
{
    // Swing is a warp of the beat, not a late off-eighth. The "&" sits on
    // the triplet at full amount; "e" and "a" ride the same stretch so a
    // 16th-grid part shuffles instead of fighting the delayed 8ths.
    float delay = 0.0f;
    if (swing > 1.0e-6f)
    {
        const int s = ((step % 4) + 4) % 4;
        const float d = swing * kFullSwingBeats;
        const float u = 0.25f * static_cast<float> (s);
        float t = u;
        if (s == 0)
            t = 0.0f;
        else if (u < 0.5f)
            t = u * (0.5f + d) / 0.5f;
        else
            t = (0.5f + d) + (u - 0.5f) * (0.5f - d) / 0.5f;
        delay += std::max (0.0f, t - u);
    }

    // Then the part that is an error. Biased late by half its spread so the
    // result never asks to be scheduled before the grid position - the clock
    // hands out positions as they pass, and there is no going back for one.
    delay += humanize * (kFeelBiasBeatsAt120 + kFeelSpreadBeatsAt120 * 0.5f * rng.nextSigned());
    return std::max (0.0f, delay);
}

bool GrooveEngine::isFillBar (int barIndex) const noexcept
{
    const StyleSpec& spec = kStyles[static_cast<int> (style)];
    return wrapBar (barIndex, spec.fillEveryBars) == spec.fillEveryBars - 1;
}

int GrooveEngine::eventsAt (int barIndex, int step, GrooveEvent* out, int maxOut) noexcept
{
    if (out == nullptr || maxOut <= 0 || step < 0 || step >= kStepsPerBar)
        return 0;

    const StyleSpec& spec = kStyles[static_cast<int> (style)];
    int n = 0;
    const int beat = step / 4;
    const float accent = spec.accent[beat & 3];

    if (shakerOn && n < maxOut)
    {
        // The subdivision setting thins the style's pattern; it never adds to
        // it, so a style that does not want sixteenths does not get them just
        // because the user asked for a busy shaker.
        bool allowed = true;
        if (shakerGrid == Subdivision::quarter)
            allowed = (step % 4) == 0;
        else if (shakerGrid == Subdivision::eighth)
            allowed = (step % 2) == 0;

        const float v = allowed ? spec.shaker[step] : 0.0f;
        if (v > 0.0f)
        {
            // Down on the pulse, up on the return. They are different strokes
            // on a real shaker, not the same one twice, and playing them as the
            // same one is what makes a shaker part sound like a click track
            // with noise on it.
            out[n].stroke = (step % 4) == 0 ? Stroke::shakerDown : Stroke::shakerUp;
            out[n].velocity = humanVelocity (v * accent);
            out[n].delayBeats = humanDelay (step);
            ++n;
        }
    }

    // The congas never play the first quarter's down-stroke.
    //
    // That is where the band already is: the kick, the bass and the downbeat of
    // whatever the guitarist is playing all land there together, and a conga on
    // top of them adds nothing that can be heard as a separate voice - it just
    // thickens an attack that was already the loudest moment in the bar. A
    // percussionist standing next to a drummer plays *around* the one: the low
    // tone lands on its "e" or its "and", the heel-toe pair starts a sixteenth
    // late, and the one itself is left to the band.
    //
    // Every table below has been written that way, and this is the guard that
    // keeps it true: it costs one comparison per pulse and it means a pattern
    // edited later cannot quietly put a stroke back on the downbeat. The shaker
    // is not covered by it - a shaker on the pulse is the pulse, and it is what
    // the listener follows.
    if (congasOn && step != 0)
    {
        // An eight-bar sentence: A B A C  D B A, then the fill. The first four
        // bars are still the original phrase - state it, answer it, state it,
        // go somewhere - and D is the extra riff that stops the second half
        // from being the first half again.
        const bool fill = isFillBar (barIndex);
        const int inPhrase = wrapBar (barIndex, 8);
        const Hit* bar = spec.barA;
        int count = spec.nA;
        if (fill)
        {
            bar = spec.fill;
            count = spec.nFill;
        }
        else if (inPhrase == 1 || inPhrase == 5)
        {
            bar = spec.barB;
            count = spec.nB;
        }
        else if (inPhrase == 3)
        {
            bar = spec.barC;
            count = spec.nC;
        }
        else if (inPhrase == 4)
        {
            bar = spec.barD;
            count = spec.nD;
        }

        for (int i = 0; i < count && n < maxOut; ++i)
        {
            if (bar[i].step != step)
                continue;
            out[n].stroke = bar[i].stroke;
            out[n].velocity = humanVelocity (bar[i].velocity * accent);
            out[n].delayBeats = humanDelay (step);
            ++n;
        }

        // Ghost notes: the barely-there fingertip strokes on the sixteenths a
        // player fills with without thinking about it. They are the difference
        // between a part that breathes and one that is merely correct, and they
        // have to stay quiet enough that you notice them only when they stop.
        // How many there are is part of the style: a pop record wants almost
        // none, a marcha is built out of them.
        if (n < maxOut && ! fill && (step % 2) == 1)
        {
            const float chance = spec.ghostChance * intensity * (0.4f + 0.6f * humanize);
            if (rng.nextFloat() < chance)
            {
                out[n].stroke = rng.nextFloat() < 0.5f ? Stroke::toe : Stroke::heel;
                out[n].velocity = humanVelocity (0.16f * accent);
                out[n].delayBeats = humanDelay (step);
                ++n;
            }
        }
    }

    return n;
}

} // namespace vp
