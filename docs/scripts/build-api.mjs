import { spawnSync } from 'node:child_process'
import { mkdirSync, rmSync } from 'node:fs'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))
const output = fileURLToPath(new URL('../public/api/', import.meta.url))
const doxygen = process.env.DOXYGEN || 'doxygen'
const options = { cwd: root, stdio: 'inherit' }

const version = spawnSync(doxygen, ['--version'], options)
if (version.error || version.status !== 0) {
  console.error('Doxygen is required. Install Doxygen 1.9.8 or newer and add it to PATH, or set DOXYGEN to its executable path.')
  process.exit(1)
}

// Remove only generated API files so renamed symbols cannot leave stale pages.
rmSync(output, { recursive: true, force: true })
mkdirSync(output, { recursive: true })

const result = spawnSync(doxygen, ['docs/doxygen/Doxyfile'], options)
if (result.error) console.error(result.error.message)
process.exit(result.status ?? 1)
