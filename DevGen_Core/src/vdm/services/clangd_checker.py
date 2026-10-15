#!/usr/bin/env python3
"""
Clangd-based C syntax error checker for QEMU projects.

This module provides functionality to check C source files for syntax errors
using clangd's Language Server Protocol (LSP) interface.
"""

import json
import subprocess
import os
import sys
import threading
import queue
from pathlib import Path
from typing import Optional, List, Dict, Any, Tuple
from dataclasses import dataclass, field
from enum import IntEnum


class DiagnosticSeverity(IntEnum):
    """LSP Diagnostic severity levels."""
    ERROR = 1
    WARNING = 2
    INFORMATION = 3
    HINT = 4


@dataclass
class Position:
    """Represents a position in a text document."""
    line: int  # 0-based
    character: int  # 0-based

    def __str__(self) -> str:
        return f"{self.line + 1}:{self.character + 1}"


@dataclass
class Range:
    """Represents a range in a text document."""
    start: Position
    end: Position

    def __str__(self) -> str:
        return f"{self.start}-{self.end}"


@dataclass
class Diagnostic:
    """Represents a diagnostic (error, warning, etc.)."""
    range: Range
    severity: DiagnosticSeverity
    message: str
    source: str = "clangd"
    code: Optional[str] = None
    snippet: str = ""

    def __str__(self) -> str:
        severity_str = self.severity.name
        return f"{self.range.start}: {severity_str}: {self.message}"

    def is_error(self) -> bool:
        """Check if this diagnostic is an error."""
        return self.severity == DiagnosticSeverity.ERROR

    def is_warning(self) -> bool:
        """Check if this diagnostic is a warning."""
        return self.severity == DiagnosticSeverity.WARNING

    def is_information(self) -> bool:
        """Check if this diagnostic is informational."""
        return self.severity == DiagnosticSeverity.INFORMATION

    def is_hint(self) -> bool:
        """Check if this diagnostic is a hint."""
        return self.severity == DiagnosticSeverity.HINT


@dataclass
class CheckResult:
    """Result of checking a source file."""
    file_path: str
    diagnostics: List[Diagnostic]
    success: bool
    error_message: Optional[str] = None

    @property
    def errors(self) -> List[Diagnostic]:
        """Return only error-level diagnostics."""
        return [d for d in self.diagnostics if d.severity == DiagnosticSeverity.ERROR]

    @property
    def warnings(self) -> List[Diagnostic]:
        """Return only warning-level diagnostics."""
        return [d for d in self.diagnostics if d.severity == DiagnosticSeverity.WARNING]

    @property
    def informations(self) -> List[Diagnostic]:
        """Return only information-level diagnostics."""
        return [d for d in self.diagnostics if d.severity == DiagnosticSeverity.INFORMATION]

    @property
    def hints(self) -> List[Diagnostic]:
        """Return only hint-level diagnostics."""
        return [d for d in self.diagnostics if d.severity == DiagnosticSeverity.HINT]

    @property
    def errors_and_warnings(self) -> List[Diagnostic]:
        """Return both errors and warnings."""
        return [d for d in self.diagnostics 
                if d.severity in (DiagnosticSeverity.ERROR, DiagnosticSeverity.WARNING)]

    @property
    def error_count(self) -> int:
        """Return the number of errors."""
        return len(self.errors)

    @property
    def warning_count(self) -> int:
        """Return the number of warnings."""
        return len(self.warnings)

    @property
    def total_issues(self) -> int:
        """Return total number of all diagnostics."""
        return len(self.diagnostics)

    def has_errors(self) -> bool:
        """Check if there are any error-level diagnostics."""
        return self.error_count > 0

    def has_hints(self) -> bool:
        """Check if there are any hint-level diagnostics."""
        return len(self.hints) > 0
    def has_warnings(self) -> bool:
        """Check if there are any warning-level diagnostics."""
        return self.warning_count > 0

    def has_issues(self) -> bool:
        """Check if there are any diagnostics (errors, warnings, etc.)."""
        return self.total_issues > 0

    def get_diagnostics_by_severity(self, severity: DiagnosticSeverity) -> List[Diagnostic]:
        """Get diagnostics filtered by severity level."""
        return [d for d in self.diagnostics if d.severity == severity]

    def get_summary(self) -> Dict[str, int]:
        """Get a summary of diagnostic counts by severity."""
        return {
            "errors": self.error_count,
            "warnings": self.warning_count,
            "informations": len(self.informations),
            "hints": len(self.hints),
            "total": self.total_issues
        }

    def to_dict(self) -> Dict[str, Any]:
        """Convert the result to a dictionary."""
        return {
            "file_path": self.file_path,
            "success": self.success,
            "error_message": self.error_message,
            "summary": self.get_summary(),
            "diagnostics": [
                {
                    "line": d.range.start.line + 1,
                    "column": d.range.start.character + 1,
                    "end_line": d.range.end.line + 1,
                    "end_column": d.range.end.character + 1,
                    "severity": d.severity.name,
                    "severity_level": int(d.severity),
                    "message": d.message,
                    "source": d.source,
                    "code": d.code,
                    "snippet": d.snippet
                }
                for d in self.diagnostics
            ]
        }


class ClangdClient:
    """
    A client for communicating with clangd via LSP.
    
    This client manages the clangd subprocess and handles LSP message
    encoding/decoding for syntax checking.
    """

    def __init__(self, build_dir: str, clangd_path: str = "clangd"):
        """
        Initialize the clangd client.

        Args:
            build_dir: Path to the QEMU build directory containing compile_commands.json
            clangd_path: Path to the clangd executable (default: "clangd")
        """
        self.build_dir = Path(build_dir).resolve()
        self.clangd_path = clangd_path
        self.process: Optional[subprocess.Popen] = None
        self.request_id = 0
        self._lock = threading.Lock()
        self._response_queue: queue.Queue = queue.Queue()
        self._notification_queue: queue.Queue = queue.Queue()
        self._reader_thread: Optional[threading.Thread] = None
        self._running = False
        self._pending_requests: Dict[int, queue.Queue] = {}
        self._diagnostics: Dict[str, List[Dict]] = {}

        # Validate build directory
        compile_commands = self.build_dir / "compile_commands.json"
        if not compile_commands.exists():
            raise FileNotFoundError(
                f"compile_commands.json not found in {self.build_dir}"
            )

    def _encode_message(self, content: Dict[str, Any]) -> bytes:
        """Encode a message for LSP communication."""
        body = json.dumps(content).encode('utf-8')
        header = f"Content-Length: {len(body)}\r\n\r\n".encode('utf-8')
        return header + body

    def _read_message(self) -> Optional[Dict[str, Any]]:
        """Read and decode an LSP message from clangd."""
        if not self.process or not self.process.stdout:
            return None

        try:
            # Read headers
            headers = {}
            while True:
                line = self.process.stdout.readline()
                if not line:
                    return None
                line = line.decode('utf-8').strip()
                if not line:
                    break
                if ':' in line:
                    key, value = line.split(':', 1)
                    headers[key.strip()] = value.strip()

            # Read content
            content_length = int(headers.get('Content-Length', 0))
            if content_length > 0:
                content = self.process.stdout.read(content_length)
                return json.loads(content.decode('utf-8'))
        except Exception as e:
            if self._running:
                print(f"Error reading message: {e}", file=sys.stderr)
        return None

    def _reader_loop(self):
        """Background thread for reading messages from clangd."""
        while self._running:
            message = self._read_message()
            if message is None:
                if self._running:
                    continue
                break

            if 'id' in message and 'method' not in message:
                # This is a response
                request_id = message['id']
                with self._lock:
                    if request_id in self._pending_requests:
                        self._pending_requests[request_id].put(message)
            elif 'method' in message:
                # This is a notification or request from server
                if message['method'] == 'textDocument/publishDiagnostics':
                    params = message.get('params', {})
                    uri = params.get('uri', '')
                    diagnostics = params.get('diagnostics', [])
                    with self._lock:
                        self._diagnostics[uri] = diagnostics
                    self._notification_queue.put(message)

    def _send_request(self, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        """Send a request to clangd and wait for response."""
        with self._lock:
            self.request_id += 1
            request_id = self.request_id
            response_queue: queue.Queue = queue.Queue()
            self._pending_requests[request_id] = response_queue

        message = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params
        }

        if self.process and self.process.stdin:
            self.process.stdin.write(self._encode_message(message))
            self.process.stdin.flush()

        try:
            response = response_queue.get(timeout=30)
            return response
        finally:
            with self._lock:
                del self._pending_requests[request_id]

    def _send_notification(self, method: str, params: Dict[str, Any]):
        """Send a notification to clangd (no response expected)."""
        message = {
            "jsonrpc": "2.0",
            "method": method,
            "params": params
        }

        if self.process and self.process.stdin:
            self.process.stdin.write(self._encode_message(message))
            self.process.stdin.flush()

    def start(self):
        """Start the clangd process."""
        if self.process is not None:
            return

        cmd = [
            self.clangd_path,
            f"--compile-commands-dir={self.build_dir}",
            "--clang-tidy=false",  # Disable clang-tidy for faster checking
            "--background-index=false",  # Disable background indexing
            "--log=error",  # Reduce log verbosity
        ]

        try:
            self.process = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
        except FileNotFoundError:
            raise RuntimeError(f"clangd not found at '{self.clangd_path}'")

        self._running = True
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()

        # Initialize LSP
        self._initialize()

    def _initialize(self):
        """Send LSP initialize request."""
        params = {
            "processId": os.getpid(),
            "rootUri": f"file://{self.build_dir}",
            "capabilities": {
                "textDocument": {
                    "publishDiagnostics": {
                        "relatedInformation": True,
                        "tagSupport": {
                            "valueSet": [1, 2]  # Unnecessary, Deprecated
                        },
                        "versionSupport": True
                    }
                }
            },
            "initializationOptions": {
                "compilationDatabasePath": str(self.build_dir)
            }
        }

        response = self._send_request("initialize", params)
        if 'error' in response:
            raise RuntimeError(f"Failed to initialize clangd: {response['error']}")

        # Send initialized notification
        self._send_notification("initialized", {})

    def stop(self):
        """Stop the clangd process."""
        self._running = False

        if self.process:
            try:
                self._send_request("shutdown", {})
                self._send_notification("exit", {})
            except Exception:
                pass

            try:
                self.process.terminate()
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
            finally:
                self.process = None

        if self._reader_thread:
            self._reader_thread.join(timeout=2)
            self._reader_thread = None

    def __enter__(self):
        """Context manager entry."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.stop()
        return False

    def _file_uri(self, file_path: str) -> str:
        """Convert a file path to a URI."""
        return f"file://{Path(file_path).resolve()}"

    def _wait_for_diagnostics(self, uri: str, timeout: float = 30.0) -> List[Dict]:
        """Wait for diagnostics to be published for a file."""
        import time
        start_time = time.time()
        last_diagnostics_time = None
        stable_wait = 0.5  # Wait this long after last diagnostic update

        while time.time() - start_time < timeout:
            # Drain notification queue and track timing
            got_new = False
            try:
                while True:
                    msg = self._notification_queue.get_nowait()
                    if msg.get('method') == 'textDocument/publishDiagnostics':
                        params = msg.get('params', {})
                        if params.get('uri') == uri:
                            got_new = True
                            last_diagnostics_time = time.time()
            except queue.Empty:
                pass

            with self._lock:
                if uri in self._diagnostics:
                    # If we have diagnostics and they've been stable for a bit, return
                    if last_diagnostics_time and (time.time() - last_diagnostics_time) > stable_wait:
                        return self._diagnostics[uri]

            time.sleep(0.1)

        # Return whatever we have after timeout
        with self._lock:
            return self._diagnostics.get(uri, [])

    def check_file(self, file_path: str, timeout: float = 30.0) -> CheckResult:
        """
        Check a C source file for syntax errors and warnings.

        Args:
            file_path: Path to the C source file to check
            timeout: Maximum time to wait for diagnostics (seconds)

        Returns:
            CheckResult containing all diagnostics (errors, warnings, etc.) and status
        """
        file_path = str(Path(file_path).resolve())
        uri = self._file_uri(file_path)

        # Verify file exists
        if not Path(file_path).exists():
            return CheckResult(
                file_path=file_path,
                diagnostics=[],
                success=False,
                error_message=f"File not found: {file_path}"
            )

        # Read file content
        try:
            with open(file_path, 'r', encoding='utf-8', errors='replace') as f:
                content = f.read()
        except Exception as e:
            return CheckResult(
                file_path=file_path,
                diagnostics=[],
                success=False,
                error_message=f"Failed to read file: {e}"
            )

        # Clear previous diagnostics for this file
        with self._lock:
            self._diagnostics.pop(uri, None)

        # Open document
        self._send_notification("textDocument/didOpen", {
            "textDocument": {
                "uri": uri,
                "languageId": "c",
                "version": 1,
                "text": content
            }
        })

        # Wait for diagnostics
        raw_diagnostics = self._wait_for_diagnostics(uri, timeout)

        # Convert to our Diagnostic objects (including ALL severity levels)
        lines = content.splitlines()
        diagnostics = []
        for diag in raw_diagnostics:
            range_data = diag.get('range', {})
            start = range_data.get('start', {})
            end = range_data.get('end', {})

            # Take 2 lines before and after
            start_line = max(0, start.get('line', 0) - 6)
            end_line = min(len(lines), end.get('line', 0) + 7)
            snippet_lines = lines[start_line:end_line]

            # Add a column pointer to the error line
            err_line_rel = start.get('line', 0) - start_line
            if 0 <= err_line_rel < len(snippet_lines):
                col = start.get('character', 0)
                snippet_lines[err_line_rel] += f"\n{' ' * col}←←← {diag.get('message', '')}"

            snippet = '\n'.join(f"{i+1+start_line:4d} | {l}"
                                for i, l in enumerate(snippet_lines))
            
            # Get severity, default to ERROR if not specified
            severity_value = diag.get('severity', 1)
            try:
                severity = DiagnosticSeverity(severity_value)
            except ValueError:
                severity = DiagnosticSeverity.ERROR

            diagnostics.append(Diagnostic(
                range=Range(
                    start=Position(
                        line=start.get('line', 0),
                        character=start.get('character', 0)
                    ),
                    end=Position(
                        line=end.get('line', 0),
                        character=end.get('character', 0)
                    )
                ),
                severity=severity,
                message=diag.get('message', ''),
                source=diag.get('source', 'clangd'),
                code=diag.get('code'),
                snippet=snippet
            ))

        # Sort diagnostics: errors first, then warnings, then by line number
        diagnostics.sort(key=lambda d: (d.severity, d.range.start.line, d.range.start.character))

        # Close document
        self._send_notification("textDocument/didClose", {
            "textDocument": {"uri": uri}
        })

        # print(diagnostics)

        return CheckResult(
            file_path=file_path,
            diagnostics=diagnostics,
            success=True
        )


def check_c_syntax(
    source_file: str,
    build_dir: str,
    clangd_path: str = "clangd",
    timeout: float = 30.0
) -> CheckResult:
    """
    Check a C source file for syntax errors and warnings using clangd.

    This is the main entry point for syntax checking. It handles starting
    and stopping the clangd service automatically.

    Args:
        source_file: Path to the C source file to check
        build_dir: Path to the QEMU build directory containing compile_commands.json
        clangd_path: Path to the clangd executable (default: "clangd")
        timeout: Maximum time to wait for diagnostics (seconds)

    Returns:
        CheckResult containing all diagnostics (errors and warnings) and status

    Example:
        >>> result = check_c_syntax(
        ...     "/path/to/qemu/hw/arm/virt.c",
        ...     "/path/to/qemu/build"
        ... )
        >>> print(f"Errors: {result.error_count}, Warnings: {result.warning_count}")
        >>> if result.has_errors():
        ...     for error in result.errors:
        ...         print(f"Error: {error}")
        >>> if result.has_warnings():
        ...     for warning in result.warnings:
        ...         print(f"Warning: {warning}")
    """
    try:
        with ClangdClient(build_dir, clangd_path) as client:
            return client.check_file(source_file, timeout)
    except Exception as e:
        return CheckResult(
            file_path=source_file,
            diagnostics=[],
            success=False,
            error_message=str(e)
        )


def check_multiple_files(
    source_files: List[str],
    build_dir: str,
    clangd_path: str = "clangd",
    timeout: float = 30.0
) -> List[CheckResult]:
    """
    Check multiple C source files for syntax errors and warnings using clangd.

    This function reuses a single clangd instance for efficiency when
    checking multiple files.

    Args:
        source_files: List of paths to C source files to check
        build_dir: Path to the QEMU build directory containing compile_commands.json
        clangd_path: Path to the clangd executable (default: "clangd")
        timeout: Maximum time to wait for diagnostics per file (seconds)

    Returns:
        List of CheckResult objects, one for each source file

    Example:
        >>> files = [
        ...     "/path/to/qemu/hw/arm/virt.c",
        ...     "/path/to/qemu/hw/arm/boot.c"
        ... ]
        >>> results = check_multiple_files(files, "/path/to/qemu/build")
        >>> for result in results:
        ...     summary = result.get_summary()
        ...     print(f"{result.file_path}: {summary['errors']} errors, {summary['warnings']} warnings")
    """
    results = []
    try:
        with ClangdClient(build_dir, clangd_path) as client:
            for source_file in source_files:
                result = client.check_file(source_file, timeout)
                results.append(result)
    except Exception as e:
        # If clangd fails to start, return error results for all files
        for source_file in source_files:
            results.append(CheckResult(
                file_path=source_file,
                diagnostics=[],
                success=False,
                error_message=str(e)
            ))
    return results


def format_diagnostics(
    result: CheckResult,
    show_errors: bool = True,
    show_warnings: bool = True,
    show_info: bool = False,
    show_hints: bool = False,
    show_summary: bool = True
) -> str:
    """
    Format diagnostics for display.

    Args:
        result: CheckResult to format
        show_errors: Whether to include errors (default: True)
        show_warnings: Whether to include warnings (default: True)
        show_info: Whether to include informational messages (default: False)
        show_hints: Whether to include hints (default: False)
        show_summary: Whether to show a summary line (default: True)

    Returns:
        Formatted string representation of diagnostics
    """
    lines = [f"File: {result.file_path}"]

    if not result.success:
        lines.append(f"  Error: {result.error_message}")
        return '\n'.join(lines)

    # Build filter based on what to show
    severity_filter = set()
    if show_errors:
        severity_filter.add(DiagnosticSeverity.ERROR)
    if show_warnings:
        severity_filter.add(DiagnosticSeverity.WARNING)
    if show_info:
        severity_filter.add(DiagnosticSeverity.INFORMATION)
    if show_hints:
        severity_filter.add(DiagnosticSeverity.HINT)

    filtered_diagnostics = [d for d in result.diagnostics if d.severity in severity_filter]

    if show_summary:
        summary = result.get_summary()
        lines.append(f"  Summary: {summary['errors']} error(s), {summary['warnings']} warning(s)")

    if not filtered_diagnostics:
        if not result.diagnostics:
            lines.append("  No issues found.")
        else:
            lines.append("  No issues matching filter criteria.")
        return '\n'.join(lines)

    lines.append("")  # Empty line before diagnostics

    for diag in filtered_diagnostics:
        severity = diag.severity.name
        code_str = f" [{diag.code}]" if diag.code else ""
        lines.append(f"  {diag.range.start}: {severity}{code_str}: {diag.message}")
        if diag.snippet:                       ### NEW
            lines.append("")                   # Blank line
            lines.append("      ---- code ----")
            for L in diag.snippet.splitlines():
                lines.append(f"      {L}")
            lines.append("      --------------")
    return '\n'.join(lines)


def get_errors_and_warnings(result: CheckResult) -> Tuple[List[Diagnostic], List[Diagnostic]]:
    """
    Convenience function to get both errors and warnings from a result.

    Args:
        result: CheckResult to extract from

    Returns:
        Tuple of (errors, warnings)

    Example:
        >>> result = check_c_syntax(source_file, build_dir)
        >>> errors, warnings = get_errors_and_warnings(result)
        >>> print(f"Found {len(errors)} errors and {len(warnings)} warnings")
    """
    return result.errors, result.warnings


# Example usage and CLI interface
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(
        description="Check C source files for syntax errors and warnings using clangd"
    )
    parser.add_argument(
        "source_files",
        nargs="+",
        help="C source file(s) to check"
    )
    parser.add_argument(
        "-b", "--build-dir",
        required=True,
        help="Path to QEMU build directory containing compile_commands.json"
    )
    parser.add_argument(
        "--clangd-path",
        default="clangd",
        help="Path to clangd executable (default: clangd)"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="Timeout for diagnostics per file in seconds (default: 30)"
    )
    parser.add_argument(
        "--errors-only",
        action="store_true",
        help="Show only errors, not warnings"
    )
    parser.add_argument(
        "--warnings-only",
        action="store_true",
        help="Show only warnings, not errors"
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Show all diagnostics including info and hints"
    )
    parser.add_argument(
        "--no-summary",
        action="store_true",
        help="Don't show summary line"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output results in JSON format"
    )

    args = parser.parse_args()

    # Check files
    if len(args.source_files) == 1:
        results = [check_c_syntax(
            args.source_files[0],
            args.build_dir,
            args.clangd_path,
            args.timeout
        )]
    else:
        results = check_multiple_files(
            args.source_files,
            args.build_dir,
            args.clangd_path,
            args.timeout
        )

    # Output results
    if args.json:
        output = []
        for result in results:
            result_dict = result.to_dict()
            
            # Filter diagnostics based on options
            if args.errors_only:
                result_dict["diagnostics"] = [
                    d for d in result_dict["diagnostics"]
                    if d["severity"] == "ERROR"
                ]
            elif args.warnings_only:
                result_dict["diagnostics"] = [
                    d for d in result_dict["diagnostics"]
                    if d["severity"] == "WARNING"
                ]
            elif not args.all:
                # Default: show errors and warnings
                result_dict["diagnostics"] = [
                    d for d in result_dict["diagnostics"]
                    if d["severity"] in ("ERROR", "WARNING")
                ]
            
            output.append(result_dict)
        print(json.dumps(output, indent=2))
    else:
        # Determine what to show
        show_errors = not args.warnings_only
        show_warnings = not args.errors_only
        show_info = args.all
        show_hints = args.all

        for result in results:
            print(format_diagnostics(
                result,
                show_errors=show_errors,
                show_warnings=show_warnings,
                show_info=show_info,
                show_hints=show_hints,
                show_summary=not args.no_summary
            ))
            print()

        # Print overall summary
        total_errors = sum(r.error_count for r in results)
        total_warnings = sum(r.warning_count for r in results)
        total_files = len(results)
        failed_files = sum(1 for r in results if not r.success)
        
        print("-" * 60)
        print(f"Total: {total_files} file(s) checked, {total_errors} error(s), {total_warnings} warning(s)")
        if failed_files:
            print(f"       {failed_files} file(s) failed to check")

    # Exit with error code if any errors found
    has_errors = any(r.has_errors() for r in results)
    has_failures = any(not r.success for r in results)
    sys.exit(1 if has_errors or has_failures else 0)
