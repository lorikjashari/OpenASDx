# Slides 10 to 13: the talk

Four slides, 4 minutes, about 1 minute each, in `swiss-ai-weeks-hackathon-2026.pptx`. Slide 3, "In summary", announces the same message at the start of the talk. They come after the vTPU slides (5 to 9) and lead into "Taking Open Hardware beyond the hackathon" (14). Two backup slides for questions come after the closing slide: "What we learned" (24) and the diagram "OpenASDx, end to end" (25). Every number comes from the FPGA logs, the same ones as `REPORT.md`.

## The message

Three points, in order, following the vTPU:

1. **We built an accelerator from scratch:** the vTPU, in the talk before ours.
2. **We took a mature open-source one, Gemmini, and ran it on real hardware in the cloud.** The chip design and all the software we wrote and ran are open source.
3. **It's a toy example, but it proves the point.** It's possible within a hackathon's time, without compromising on open source.

Point 3 also answers the survey on slide 18 ("too hard, presented as impossible with no final result"): here is a result.

If time runs short, say the three points and skip the rest.

**Two words to keep accurate, because a judge may check them:**
- Say **"on real hardware, in the cloud"**, not "in production". It's a rented FPGA running a demo, not a service people rely on.
- Say **"the chip design and all the software are open source"**, not "only open source". The FPGA, AWS's FPGA layer and the tools that made the prebuilt FPGA image are proprietary. Only the rented hardware isn't open.

## Slide by slide

### 10. From our own accelerator to a mature open one (about 1 min)

**Message (points 1 and 2):** we built one from scratch to learn, then ran a mature open one on real hardware.

- "You just saw the vTPU: an accelerator our team built from scratch, to learn how it works inside."
- "Then we took the next step, with the same model and the same prompt."
- "We took Gemmini, a more mature open-source accelerator from UC Berkeley that works like a TPU."
- "And we ran it on real hardware: an FPGA, a chip that can be rewired to become any circuit, rented from AWS in Frankfurt."

Don't compare the two projects' speeds. The vTPU's speed is the speed of a simulation, and that's not the point.

### 11. It runs on real hardware (about 1 min)

**Message (point 2):** it works, on real hardware, with an open design and open software.

- "This is the FPGA, in real time: we give it 'Once upon a time', and it writes the story on its own, word by word."
- "The accelerator does the multiplications, and a small processor next to it does the rest."
- "The chip design and all the software are open source. Only the rented hardware isn't."

If there's a live demo, say "you'll see it live at the end", and move on quickly.

### 12. The accelerator makes it 2.3× faster (about 1 min)

**Message:** we used Gemmini as it is, and did all the work to run a language model on it. It paid off.

- "Gemmini wasn't built for language models, and we didn't change it."
- "Our work was everything around it: converting the model to Gemmini's 8-bit format, writing the software that runs it, and deploying it on a cloud FPGA."
- "The result: 9.0 word pieces per second with the accelerator, 3.9 without it. The multiplications themselves got 35 times faster."

Say "2.3 times faster", never "230% faster": that would be wrong (it's 130% faster). Don't mention clock speeds unless asked. If asked: the FPGA runs at 30 MHz, far slower than a real chip. The same design made as a real chip at 1 GHz would take the same steps, and write about 300 word pieces per second. That's a projection, not a measurement.

### 13. A toy example, but it proves the point (about 1 min)

**Message (point 3, and the recap):** it's a toy, but it's possible, in a hackathon, with open source.

- "This is a toy: a tiny model, on an FPGA that imitates a real chip."
- "But it proves the point. We built an accelerator from scratch, to learn how it works. Then we took a mature open-source one and ran it on real hardware."
- "And it was possible within a hackathon's time, without compromising on open source anywhere in the chip design or the software."

Then hand over to slide 14. Don't claim that the next gains are in software: our measurements don't show that (see the questions below).

## Words to explain

Worth explaining if they come up:
- **Accelerator:** a chip, or part of one, built to do one job fast. Here, the multiplications of an AI model.
- **Word piece (token):** what a language model writes, one at a time: a word or part of a word.
- **Open source:** a design or code that anyone can read, use and improve.

## Questions we may get

- **"Is it really running on hardware?"** Yes: an FPGA in AWS's Frankfurt data center. The processor and the accelerator are built into it from their open-source designs.
- **"Are the results right?"** It writes exactly the same story as a laptop. We checked all 3,944 multiplications of one story against the processor's own answers, and they're identical.
- **"How does it compare with a laptop or a GPU?"** A laptop is far faster and uses far less energy per word piece, because an FPGA imitates a chip, slowly. The point isn't speed: it's that an open design runs for real. The numbers are in `REPORT.md`, "Against a laptop".
- **"Is it really all open source?"** The chip design (the Rocket processor and the Gemmini accelerator), the tools that build and run it (Chipyard, FireSim), and all our code: yes. The FPGA itself, AWS's layer around it, and the Xilinx tools that made the prebuilt FPGA image aren't. That's the rented hardware.
- **"Where does the time go?"** We measured it on the FPGA. The accelerator finishes its part in 4% of each word piece's time, and the small processor around it takes the rest, mostly preparing data for it. Better software could make it up to 2× faster. Bigger gains would need hardware choices too: a faster processor, more of the steps on the accelerator (as the vTPU does), and faster memory. So the lesson is that the accelerator isn't the bottleneck any more; the work around it is. Details: `REPORT.md`, "Profile".
- **"Why not put the vTPU on the FPGA?"** That's a natural next step. A design has to be prepared for real hardware first, and Gemmini already was.

## Backup slides

- **24, "What we learned":** the profile, for "where does the time go?" (the answer is under "Questions we may get"). Its last line, "Everything we used is open source: the processor, the accelerator and the tools", overclaims: the FPGA tools aren't open source. Better: "The chip design and all the software: open source".
- **25, "OpenASDx, end to end":** the diagram, for "how does it work?". Walk it left to right: our laptop (the model, quantized to 8-bit, and the C program), Chipyard (the chip and the tools), and AWS (the FireSim manager loads the chip and the program onto the FPGA, which writes the story).
