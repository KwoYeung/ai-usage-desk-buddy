// Passport DSH activity: expose only agent working/turn-completion state.
// No prompt, answer, tool argument, or credential content is read or written.
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

export const name = 'passport-dsh-activity'
export const inject = ['agents']

const stateDir = path.join(os.homedir(), '.dsh')
const statePath = path.join(stateDir, 'passport-state.json')

function readNumber(value, fallback) {
  return Number.isSafeInteger(value) && value >= 0 ? value : fallback
}

function loadPrevious() {
  try {
    const value = JSON.parse(fs.readFileSync(statePath, 'utf8'))
    return {
      completion: readNumber(value.completion_seq, 0),
      turn: readNumber(value.turn, 0),
    }
  } catch {
    return { completion: 0, turn: 0 }
  }
}

export function apply(ctx) {
  const previous = loadPrevious()
  let completion = previous.completion
  let lastTurn = previous.turn
  let working = false
  const running = new Set()

  const isTopLevel = (agent) => {
    const depth = agent?.session?.header?.delegationDepth
    return depth === undefined || depth === 0
  }
  const agentId = (agent) => String(agent?.session?.id ?? '')

  const write = (available) => {
    try {
      fs.mkdirSync(stateDir, { recursive: true })
      const payload = {
        available: available === true,
        working: available === true && working,
        completion_seq: completion,
        turn: lastTurn,
        updated: Date.now(),
      }
      const temporary = statePath + '.tmp'
      fs.writeFileSync(temporary, JSON.stringify(payload), { mode: 0o600 })
      fs.renameSync(temporary, statePath)
    } catch {
      // The bridge treats a missing/stale file as unavailable.
    }
  }

  for (const agent of ctx.agents.list()) {
    if (agent.status === 'running' && isTopLevel(agent)) running.add(agentId(agent))
  }
  working = running.size > 0
  write(true)

  ctx.on('agent/status', ({ agent, status }) => {
    if (!isTopLevel(agent)) return
    const id = agentId(agent)
    if (status === 'running') running.add(id)
    else running.delete(id)
    working = running.size > 0
    write(true)
  })

  ctx.on('agent/turn-stopping', ({ agent, turn, signal }) => {
    if (!isTopLevel(agent) || signal?.aborted) return
    // A top-level turn is completing naturally; one completion event per turn.
    lastTurn = readNumber(turn, lastTurn)
    completion += 1
    write(true)
  })

  ctx.effect(() => {
    const timer = setInterval(() => write(true), 10000)
    return () => {
      clearInterval(timer)
      working = false
      write(false)
    }
  })
}
