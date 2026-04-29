Attribute VB_Name = "Module13"
' ============================================================
' Krystian Tutoring -- Full Automation Module v3
' ============================================================
' SETUP REQUIRED:
'   1. Replace YOUR_BSB and YOUR_ACCOUNT with real bank details
'   2. Replace MACRODROID_WEBHOOK_URL with your MacroDroid webhook URL
' ============================================================

Const YOUR_BSB As String = "XXX-XXX"
Const YOUR_ACCOUNT As String = "XXXXXXXXX"
Const YOUR_NAME As String = "Krystian"
Const MACRODROID_WEBHOOK_URL As String = "https://trigger.macrodroid.com/YOUR_ID_HERE/krystian_sms"

' ============================================================
' SESSION LOG RESOLVE
' ============================================================
Sub ResolveSession(sourceRow As Long)
    Dim wsSL2 As Worksheet, wsSL As Worksheet
    Set wsSL2 = ThisWorkbook.Sheets("Session Log (2)")
    Set wsSL = ThisWorkbook.Sheets("Session Log")

    If Trim(CStr(wsSL2.Cells(sourceRow, 2).Value)) = "" Then
        MsgBox "No parent name found on this row.", vbExclamation
        Exit Sub
    End If

    Dim statusVal As String
    statusVal = Trim(wsSL2.Cells(sourceRow, 10).Value)
    If UCase(statusVal) = "RESOLVED" Then
        MsgBox "This session has already been resolved.", vbInformation
        Exit Sub
    End If

    ' Find next empty row in Session Log (data starts row 3)
    Dim destRow As Long
    destRow = 3
    Do While wsSL.Cells(destRow, 2).Value <> ""
        destRow = destRow + 1
        If destRow > 5000 Then Exit Do
    Loop

    ' Copy values from Session Log (2)
    wsSL.Cells(destRow, 1).Value = wsSL2.Cells(sourceRow, 1).Value
    wsSL.Cells(destRow, 1).NumberFormat = "dd/mm/yyyy hh:mm"
    wsSL.Cells(destRow, 2).Value = wsSL2.Cells(sourceRow, 2).Value
    If wsSL2.Cells(sourceRow, 8).Value <> "" Then
        wsSL.Cells(destRow, 8).Value = wsSL2.Cells(sourceRow, 8).Value
    End If
    If wsSL2.Cells(sourceRow, 9).Value <> "" Then
        wsSL.Cells(destRow, 9).Value = wsSL2.Cells(sourceRow, 9).Value
    End If

    ' Mark resolved in Session Log (2)
    wsSL2.Cells(sourceRow, 10).Value = "Resolved"
    wsSL2.Cells(sourceRow, 10).Interior.Color = RGB(197, 224, 180)
    wsSL2.Cells(sourceRow, 10).Font.Color = RGB(56, 87, 35)
    wsSL2.Cells(sourceRow, 10).Font.Bold = True

    ' Grey out resolved row
    Dim c As Integer
    For c = 1 To 9
        wsSL2.Cells(sourceRow, c).Interior.Color = RGB(217, 217, 217)
    Next c

    MsgBox "Session resolved!" & Chr(10) & _
           "Parent: " & wsSL2.Cells(sourceRow, 2).Value & Chr(10) & _
           "Copied to Session Log row " & destRow & ".", _
           vbInformation, "Session Resolved"
End Sub

' ============================================================
' RESOLVE ROW CLICK HANDLERS
' ============================================================
Sub ClickResolveRow2():  Call ResolveSession(2):  End Sub
Sub ClickResolveRow3():  Call ResolveSession(3):  End Sub
Sub ClickResolveRow4():  Call ResolveSession(4):  End Sub
Sub ClickResolveRow5():  Call ResolveSession(5):  End Sub
Sub ClickResolveRow6():  Call ResolveSession(6):  End Sub
Sub ClickResolveRow7():  Call ResolveSession(7):  End Sub
Sub ClickResolveRow8():  Call ResolveSession(8):  End Sub
Sub ClickResolveRow9():  Call ResolveSession(9):  End Sub
Sub ClickResolveRow10(): Call ResolveSession(10): End Sub
Sub ClickResolveRow11(): Call ResolveSession(11): End Sub
Sub ClickResolveRow12(): Call ResolveSession(12): End Sub
Sub ClickResolveRow13(): Call ResolveSession(13): End Sub
Sub ClickResolveRow14(): Call ResolveSession(14): End Sub
Sub ClickResolveRow15(): Call ResolveSession(15): End Sub
Sub ClickResolveRow16(): Call ResolveSession(16): End Sub
Sub ClickResolveRow17(): Call ResolveSession(17): End Sub
Sub ClickResolveRow18(): Call ResolveSession(18): End Sub
Sub ClickResolveRow19(): Call ResolveSession(19): End Sub
Sub ClickResolveRow20(): Call ResolveSession(20): End Sub

' ============================================================
' GENERATE MESSAGES
' ============================================================
Sub GenerateMessages()
    Dim wsMC As Worksheet, wsRS As Worksheet
    Set wsMC = ThisWorkbook.Sheets("Message Centre")
    Set wsRS = ThisWorkbook.Sheets("Ready to Send")

    Dim msgType As String
    msgType = Trim(wsMC.Range("D3").Value)
    If msgType = "" Then
        MsgBox "Please select a Message Type from the dropdown in cell D3.", vbExclamation
        Exit Sub
    End If

    ' Clear previous messages
    wsRS.Range("A4:E500").ClearContents
    Dim r As Long
    For r = 4 To 500
        wsRS.Rows(r).RowHeight = 60
    Next r

    Dim outputRow As Long
    outputRow = 4
    Dim smsCount As Long, fbCount As Long
    smsCount = 0
    fbCount = 0

    ' Loop all Message Centre rows dynamically until blank parent name
    Dim i As Long
    i = 5
    Do While Trim(CStr(wsMC.Cells(i, 2).Value)) <> ""
        Dim sendFlag As String
        sendFlag = Trim(CStr(wsMC.Cells(i, 1).Value))

        If UCase(sendFlag) = "YES" Then
            Dim parentName As String, children As String
            Dim contactMeth As String, phone As String
            Dim sessionDate As String, amountDue As String

            parentName  = Trim(CStr(wsMC.Cells(i, 2).Value))
            children    = Trim(CStr(wsMC.Cells(i, 3).Value))
            contactMeth = Trim(CStr(wsMC.Cells(i, 4).Value))
            phone       = Trim(CStr(wsMC.Cells(i, 5).Value))

            ' Session date
            Dim sdVal As Variant
            sdVal = wsMC.Cells(i, 6).Value
            If IsDate(sdVal) And sdVal <> "" Then
                sessionDate = Format(CDate(sdVal), "dddd d MMMM")
            Else
                sessionDate = "[session date not found -- resolve session first]"
            End If

            ' Amount due
            Dim adVal As Variant
            adVal = wsMC.Cells(i, 7).Value
            If IsNumeric(adVal) And adVal > 0 Then
                amountDue = "$" & Format(adVal, "0.00")
            Else
                amountDue = "[amount]"
            End If

            Dim msg As String
            msg = BuildMessage(msgType, parentName, children, sessionDate, amountDue)

            ' Write to Ready to Send
            wsRS.Cells(outputRow, 1).Value = parentName
            wsRS.Cells(outputRow, 2).Value = contactMeth
            If phone <> "" Then
                wsRS.Cells(outputRow, 3).Value = phone
            Else
                wsRS.Cells(outputRow, 3).Value = "Facebook Messenger"
            End If
            wsRS.Cells(outputRow, 4).Value = msg

            ' Style row
            wsRS.Rows(outputRow).RowHeight = 75
            Dim bgCol As Long
            If InStr(LCase(contactMeth), "facebook") > 0 Then
                bgCol = RGB(255, 242, 204)
                fbCount = fbCount + 1
            Else
                bgCol = RGB(226, 239, 218)
                smsCount = smsCount + 1
            End If
            wsRS.Range(wsRS.Cells(outputRow, 1), wsRS.Cells(outputRow, 4)).Interior.Color = bgCol
            wsRS.Cells(outputRow, 4).WrapText = True
            wsRS.Cells(outputRow, 4).Font.Name = "Arial"
            wsRS.Cells(outputRow, 4).Font.Size = 10

            wsMC.Cells(i, 8).Value = Now()
            wsMC.Cells(i, 8).NumberFormat = "dd/mm/yyyy hh:mm"

            outputRow = outputRow + 1
        End If
        i = i + 1
    Loop

    If outputRow = 4 Then
        MsgBox "No clients selected. Tick 'Yes' in the Send? column first.", vbInformation
        Exit Sub
    End If

    wsRS.Activate
    wsRS.Range("A1").Select

    If smsCount > 0 Then
        Dim doSend As Integer
        doSend = MsgBox(outputRow - 4 & " message(s) generated." & Chr(10) & Chr(10) & _
                        smsCount & " SMS client(s) -- click YES to send via MacroDroid now." & Chr(10) & _
                        fbCount & " Facebook client(s) -- will show pop-up to copy." & Chr(10) & Chr(10) & _
                        "Send SMS messages now?", vbYesNo + vbQuestion, "Send Messages?")
        If doSend = vbYes Then Call SendAllSMS
    Else
        MsgBox outputRow - 4 & " message(s) generated." & Chr(10) & _
               "All are Facebook clients -- use the pop-up copy buttons.", _
               vbInformation, "Messages Ready"
        Call ShowFacebookPopups
    End If
End Sub

' ============================================================
' SEND ALL SMS via MacroDroid
' ============================================================
Sub SendAllSMS()
    Dim wsRS As Worksheet
    Set wsRS = ThisWorkbook.Sheets("Ready to Send")

    If InStr(MACRODROID_WEBHOOK_URL, "YOUR_ID_HERE") > 0 Then
        MsgBox "MacroDroid webhook URL not configured yet." & Chr(10) & _
               "See the MacroDroid Setup sheet for instructions.", vbExclamation, "Setup Required"
        Exit Sub
    End If

    Dim r As Long, sentCount As Long, failCount As Long
    sentCount = 0
    failCount = 0

    For r = 4 To 500
        If Trim(CStr(wsRS.Cells(r, 2).Value)) = "" Then Exit For
        Dim contactMeth As String
        contactMeth = Trim(CStr(wsRS.Cells(r, 2).Value))
        Dim phone As String, msgText As String
        phone   = Trim(CStr(wsRS.Cells(r, 3).Value))
        msgText = Trim(CStr(wsRS.Cells(r, 4).Value))

        If InStr(LCase(contactMeth), "sms") > 0 And phone <> "" Then
            If SendWebhookSMS(phone, msgText) Then
                wsRS.Cells(r, 5).Value = "Sent"
                wsRS.Cells(r, 5).Font.Color = RGB(56, 87, 35)
                wsRS.Cells(r, 5).Font.Bold = True
                sentCount = sentCount + 1
            Else
                wsRS.Cells(r, 5).Value = "Failed"
                wsRS.Cells(r, 5).Font.Color = RGB(192, 0, 0)
                failCount = failCount + 1
            End If
        ElseIf InStr(LCase(contactMeth), "facebook") > 0 Then
            Call ShowFacebookPopup(CStr(wsRS.Cells(r, 1).Value), msgText)
        End If
    Next r

    Dim summary As String
    summary = "SMS sending complete." & Chr(10) & sentCount & " sent via MacroDroid."
    If failCount > 0 Then
        summary = summary & Chr(10) & failCount & " failed -- check phone has internet and MacroDroid is running."
    End If
    MsgBox summary, vbInformation, "Send Complete"
End Sub

' ============================================================
' SEND SINGLE SMS via MacroDroid Webhook
' ============================================================
Function SendWebhookSMS(phoneNum As String, msgText As String) As Boolean
    On Error GoTo errHandler
    Dim fullURL As String
    fullURL = MACRODROID_WEBHOOK_URL & "?phone=" & Replace(phoneNum, " ", "") & "&message=" & EncodeURL(msgText)
    Dim http As Object
    Set http = CreateObject("WinHttp.WinHttpRequest.5.1")
    http.Open "GET", fullURL, False
    http.SetTimeouts 5000, 5000, 10000, 10000
    http.Send
    SendWebhookSMS = (http.Status = 200)
    Exit Function
errHandler:
    SendWebhookSMS = False
End Function

' ============================================================
' URL ENCODE helper
' ============================================================
Function EncodeURL(txt As String) As String
    Dim i As Long, c As String, code As Integer, result As String
    result = ""
    For i = 1 To Len(txt)
        c = Mid(txt, i, 1)
        code = Asc(c)
        Select Case code
            Case 48 To 57, 65 To 90, 97 To 122
                result = result & c
            Case 32
                result = result & "+"
            Case Else
                result = result & "%" & Hex(code)
        End Select
    Next i
    EncodeURL = result
End Function

' ============================================================
' FACEBOOK POP-UP
' ============================================================
Sub ShowFacebookPopup(parentName As String, msgText As String)
    On Error Resume Next
    Dim clip As Object
    Set clip = CreateObject("HTMLFile")
    If Not clip Is Nothing Then
        clip.parentWindow.clipboardData.setData "text", msgText
    End If
    On Error GoTo 0

    MsgBox "Facebook message for " & parentName & ":" & Chr(10) & Chr(10) & _
           String(50, "-") & Chr(10) & msgText & Chr(10) & String(50, "-") & Chr(10) & Chr(10) & _
           "Message copied to clipboard -- paste into Facebook Messenger.", _
           vbInformation, "Facebook Message -- " & parentName
End Sub

Sub ShowFacebookPopups()
    Dim wsRS As Worksheet
    Set wsRS = ThisWorkbook.Sheets("Ready to Send")
    Dim r As Long
    For r = 4 To 500
        If Trim(CStr(wsRS.Cells(r, 2).Value)) = "" Then Exit For
        If InStr(LCase(wsRS.Cells(r, 2).Value), "facebook") > 0 Then
            Call ShowFacebookPopup(CStr(wsRS.Cells(r, 1).Value), CStr(wsRS.Cells(r, 4).Value))
        End If
    Next r
End Sub

' ============================================================
' BUILD MESSAGE
' ============================================================
Function BuildMessage(msgType As String, parentName As String, _
                      children As String, sessionDate As String, _
                      amountDue As String) As String
    Dim msg As String
    Select Case msgType
        Case "Session Confirmation"
            msg = "Hi " & parentName & ", just confirming " & children & _
                  "'s tutoring session on " & sessionDate & ". " & _
                  "Please let me know if anything changes. Looking forward to it!" & Chr(10) & _
                  "- " & YOUR_NAME

        Case "Payment Reminder"
            msg = "Hi " & parentName & ", just a friendly reminder that payment of " & _
                  amountDue & " for " & children & "'s session on " & sessionDate & " is now due. " & _
                  "Please transfer to: BSB: " & YOUR_BSB & " | Acc: " & YOUR_ACCOUNT & _
                  " | Ref: " & parentName & " Tutoring." & Chr(10) & _
                  "Thanks so much!" & Chr(10) & "- " & YOUR_NAME

        Case "Overdue Payment Reminder"
            msg = "Hi " & parentName & ", hope you're well! Just following up on the payment of " & _
                  amountDue & " for " & children & "'s session on " & sessionDate & _
                  " - it doesn't appear to have come through yet. No stress, please transfer when " & _
                  "you get a chance: BSB: " & YOUR_BSB & " | Acc: " & YOUR_ACCOUNT & "." & Chr(10) & _
                  "Thanks!" & Chr(10) & "- " & YOUR_NAME

        Case Else
            msg = "[Unknown message type selected]"
    End Select
    BuildMessage = msg
End Function
