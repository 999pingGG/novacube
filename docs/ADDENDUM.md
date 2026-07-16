# Development addendum

This document logs my development stories, decisions, bugs, long-form explanations, etc. that would otherwise create
long comments in the source. This part isn't AI-written or even reviewed. I want to tell my stories directly
human-to-human.

## Pixel-sized holes all over the terrain

The terrain was getting plagued by lots of unrasterized pixels that appeared and appeared randomly as the camera moved
around and looked like static interference, especially on mobiles. It was because the greedy mesher generates big quads
whose edges align with those of smaller quads, creating T-junctions, and apparently 4 or even 8 `subPixelPrecisionBits`
aren't enough to guarantee no pixel is uncovered by rasterization where these edges meet. Conservative rasterization
could have dead-easy fixed it, but it's not widely supported, at least on mobiles. I can't scale my quads by 1.00001 or
something like that, because we don't have their origin at hand and for other math reasons I honestly don't understand.
I tried it somehow, but it didn't work.

Even with AI the fix wasn't easy, not with all the characteristics I want/need. took quite a few iterations of
prompting GPT-5.6 Sol on the "High" reasoning setting, testing for correctness AND performance, maybe tuning some
constants, and giving feedback to the model.

Say what you will about AI. But the necessary math to solve my very specific problem in a highly-performant way that
works across most GPUs (I hope!) and just doesn't throw my entire highly-optimized greedy mesher out the window escapes
my understanding. This AI model was able to fix it just the way I needed. A thing I very likely couldn't have done
without AI. Now the problem is fixed, and I can carry on developing my game.

If you're wondering about the performance loss of the fix, because it looks like half of the complexity of the terrain
vertex shader is dedicated to this, you can just assign the initial computation of `clip_position` to `gl_Position` and
throw away the rest of the code. Change the fragment shader to output a plain black color and the clear color to white.
Look at any floor or wall that doesn't have anything else between it and the sky and witness the snow storm and the
performance difference. I tried this in a crap phone and lost only 2 or 3 FPS, around the 25 FPS mark, with a render
distance of 10 chunks. By the way, these specific performance numbers are valid only today, at the time of writing, for
this phone and for having implemented basically nothing but the renderer. You can also revert to commit `733196c` for
easier visualization of the bug.
