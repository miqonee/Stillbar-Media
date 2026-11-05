param(
 [Parameter(Mandatory=$true)][ValidatePattern('^[a-p]{32}$')][string]$ExtensionId,
 [Parameter(Mandatory=$true)][ValidatePattern('^https://addons\.opera\.com/en/extensions/details/[a-z0-9][a-z0-9-]*/$')][string]$ListingUrl
)
$ErrorActionPreference='Stop'
$taskbarListing=$ListingUrl
$taskbarResponse=Invoke-WebRequest -Uri $taskbarListing -MaximumRedirection 3
if($taskbarResponse.StatusCode -ne 200 -or $taskbarResponse.Content -notmatch 'Stillbar\s+Media'){
 throw 'Страница опубликованной Stillbar Media не подтверждена; ссылка установки не изменена.'
}
$taskbarHeader=Join-Path $PSScriptRoot '..\src\browser_store.hpp'
$taskbarContents=[IO.File]::ReadAllText($taskbarHeader)
$taskbarContents=[regex]::Replace($taskbarContents,'store_extension_id=L"[a-p]*"','store_extension_id=L"'+$ExtensionId+'"')
$taskbarContents=[regex]::Replace($taskbarContents,'store_listing_url=L"[^"]*"','store_listing_url=L"'+$ListingUrl+'"')
[IO.File]::WriteAllText($taskbarHeader,$taskbarContents,[Text.UTF8Encoding]::new($false))
Write-Output "Привязана страница $taskbarListing. Пересоберите Stillbar Media для выпуска."
