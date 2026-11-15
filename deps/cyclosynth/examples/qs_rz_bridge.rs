// Author: Codex (GPT-6 Sol), 2 October 2026.
// Line protocol: input = radians epsilon; output = verified_distance gates.
use cyclosynth::synthesis::{angle::Angle, Synthesizer};
use std::io::{self, BufRead, Write};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut stdout = io::BufWriter::new(io::stdout().lock());
    for line in io::stdin().lock().lines() {
        let line = line?;
        let values: Vec<_> = line.split_whitespace().collect();
        if values.len() != 2 {
            return Err("expected radians and epsilon".into());
        }
        let angle: f64 = values[0].parse()?;
        let epsilon: f64 = values[1].parse()?;
        if !angle.is_finite() || !epsilon.is_finite() || !(1e-48..1.0).contains(&epsilon) {
            return Err("invalid angle or epsilon (require 1e-48 <= epsilon < 1)".into());
        }
        let result = Synthesizer::new(epsilon, true)
            .synthesize_rz(Angle::Rad(angle))
            .ok_or("synthesis found no circuit")?;
        if !result.distance.is_finite() || result.distance >= epsilon {
            return Err("synthesis did not meet requested epsilon".into());
        }
        // Upstream README: ABC denotes matrix product A*B*C (C acts first).
        // Reverse to output chronological application order for column kets.
        // Q = sqrt(T) = V; express T/Tdag as VV/vv for Clifford+V output.
        let mut gates = String::new();
        for gate in result.gates.ok_or("gate extraction failed")?.chars().rev() {
            gates.push_str(match gate {
                'Q' => "V",
                'q' => "v",
                'T' => "VV",
                't' => "vv",
                'H' => "H",
                'S' => "S",
                's' => "s",
                'X' => "X",
                'Y' => "Y",
                'Z' => "Z",
                _ => return Err(format!("unexpected upstream gate {gate}").into()),
            });
        }
        writeln!(
            stdout,
            "{:.17e} {}",
            result.distance,
            if gates.is_empty() { "-" } else { &gates }
        )?;
        stdout.flush()?;
    }
    Ok(())
}
