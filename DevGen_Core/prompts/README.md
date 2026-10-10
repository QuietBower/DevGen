# Placeholders to Replace

## Step1

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **PCI_TEMPLATE** — the base modeling template

3. **Relevant_Source** — the relevant source code

## Step1_iteration

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **Relevant_Source** — the code required from the `needed_sources` produced in the previous modeling step

## Step2

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field returned by the previous step

3. **Relevant_Source** — the driver source code

## Step2_iteration

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **Relevant_Source** — the code required from the `needed_sources` produced in the previous modeling step

## Step3

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **SYNTAX_ERROR_MESSAGE** — the clangd compilation error message

4. **REPAIR_CASES** — the relevant repair case library; include it if available, otherwise leave it empty

## Step3_iteration

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **SYNTAX_ERROR_MESSAGE** — the clangd compilation error message

4. **Relevant_Source** — the code required from the `needed_sources` produced in the previous modeling step

5. **REPAIR_CASES** — the relevant repair case library; include it if available, otherwise leave it empty

## modify_after_build_fail

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **FAIL_LOGS** — the runtime compilation error log

## modify_after_build_fail_iteration

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **FAIL_LOGS** — the runtime compilation error log

4. **Relevant_Source** — the code required from the `needed_sources` produced in the previous modeling step

## Step4

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **ERR_MSG** — the error message from a failed load

4. REPAIR_CASES — the relevant repair case library; include it if available, otherwise leave it empty

## Step4_iteration

1. **DRIVER_PATH** — the path of the modeling example to be substituted

2. **QEMU_CODE** — the code from the `code` field produced in the previous modeling step

3. **ERR_MSG** — the error message from a failed load

4. **Relevant_Source** — the code required from the `needed_sources` produced in the previous modeling step

5. **REPAIR_CASES** — the relevant repair case library; include it if available, otherwise leave it empty



1. **Only Step3/Step4 have the case library. Only the iteration parts of Step3/Step4 require the relevant source code to be provided.**

2. **Only provide a single example**
